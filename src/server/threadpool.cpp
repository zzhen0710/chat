#include "threadpool.h"

// 构造：创建 n 个工作线程
explicit ThreadPool::ThreadPool(size_t n = 4) 
    : stop_(false)
{
    if (n == 0 || n > 1024) {   // 1024 已约 8GB，防呆
        throw std::invalid_argument("线程数必须在 1~1024");
    }
    // 循环创建 n 个线程，每个跑 worker()
    for (size_t i = 0; i < n; ++i) {
        // emplace_back：直接在这块内存上"原地构造 std::thread"。
        // 构造开销和"造临时对象"一样，但省掉了"临时对象 → 移动进容器"这一步
        //   （既没有临时对象，也没有移动）。
        // 不用 std::move：此时还没有"实际对象"，参数(worker指针/this)
        //   直接转发给 std::thread 的构造函数，原地造出线程对象。
        workers_.emplace_back(&ThreadPool::worker, this);
        // emplace_back：直接在 vector 里构造 thread
        /*
        template <class F, class... Args>
        explicit thread(F&& f, Args&&... args);
        */
        // &ThreadPool::worker：成员函数指针
        // this：告诉 worker"操作哪个 ThreadPool 对象"
    }
}

// 析构：停止线程池 + 回收线程
// 注意：析构"不是"直接销毁线程（线程是独立执行流，析构管不了它）；
//       析构只能"阻塞等待"（join）。所以必须先置 stop_、唤醒所有线程，
//       让它们"从等待中醒来、看到 stop_、快速 return"，否则：
//       - 没任务时，线程都卡在 cv_.wait 睡眠
//       - join 会"永远等"，析构卡死
ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mtx_);   // 拿锁
        stop_ = true;                             // 置"要停"（线程醒来的条件之一）
    }                                             // 出块 - 自动解锁

    cv_.notify_all();                             // 唤醒所有睡眠线程

    for (auto& t : workers_) {                    // 遍历所有线程
        // 本项目里线程都在 workers_ 中统一管理，实际一定 joinable，无需担心；
        // 但若某线程被 detach（脱离管理），对不可 join 的线程 join() 会抛异常，故加此判断防御
        if (t.joinable()) t.join();               // 阻塞等待它们退出
    }
}                              

// 提交任务：入队 + 唤醒一个线程
void ThreadPool::add_task(Task task) {
    {
        std::lock_guard<std::mutex> lock(mtx_);   // 拿锁，保护 stop_ 和 tasks_
        // 必须先持锁再读 stop_（不能放锁外）：
        //   stop_ 和 tasks_ 一起被 mtx_ 保护；锁外读 = 数据竞争：
        //   1.读到"旧值"（缓存没同步）——"已停"却以为"没停"，入队了没人执行
        //   2.读到"写一半"（写没完成）——未定义行为
        //   3.编译器 / CPU 重排、乱序——"顺序"不保证
        if (stop_) return;                        // 已停，拒绝新任务
        tasks_.push(std::move(task));             // 入队
    }                                             // 出作用域，自动放锁
    // 放锁外：先出 lock 作用域（放锁）、再 notify。
    //
    // 对比"放锁内"（notify 写在 lock 作用域内）：
    //   notify 跑完，本线程可能还没出作用域、锁还占着。
    //   此时若被唤醒的线程"恰好"立刻被调度（多核上并行、或单核时间片
    //   正好切过去），它"醒来即要拿锁、却拿不到"，只能多阻塞等一轮。
    // 放锁外则 notify 时锁已放，线程醒来时锁是空的，能立刻拿锁取任务。
    cv_.notify_one();
}

// 工作线程：循环取任务、执行
void ThreadPool::worker() {
    // 关于 while 条件（优雅关闭）：
    //   这里用 while(true)，而非 while(!stop_)，是刻意的——stop_ 只代表"停止请求"不代表"立刻退出"；
    //   即实际决策应由线程自身决定，在线程看到停止请求后，还需检查是否有任务还未执行完成，若有，则需执行完成后才退出，
    //   此即 notify_one 只是唤醒，而不是直接决断的原因（"notify 只'标记可运行 + 让 wait 重新检查'，不'直接让线程退出/取任务'；
    //   线程'醒来后'还要'自己查 pred、自己决定'继续等 / 取任务 / 退出'。——这就是"唤醒 ≠ 决策"），
    //   线程的退出取决于 2 个条件同时成立的"协议式退出"，而不止于线程池关闭。
    //   可以认为线程池 stop_ 置 true 意指进入关闭进程，收尾处理后正式退出，而不只是置一个标志位后立即退出，这也更具现实意义！
    //   若外层用 while(!stop_)，
    //   一旦 stop_ 置位就跳出，会丢掉 tasks_ 里尚未执行的任务。
    //   真正"优雅退出"要放在 wait 之后判断（见下方 if），保证"先清空队列"。
    while (true) {
        Task task;                              // 放循环外，减少每轮构造

        {
            std::unique_lock<std::mutex> lock(mtx_);   // 拿锁（cv_.wait 要用 unique_lock）

            // 唤醒都基于"先被 wait 阻塞"：
            //   工作线程在创建后无任务可取,在先行构造后统一卡在此处或此处之前，必须先用 cv_.wait 进入等待；
            //   之后的 notify（add_task 的 notify_one / 析构的 notify_all）才作用在"已 wait 的线程"上
            //   把它唤醒、重新检查谓词。
            //   谓词含 stop_，是为了"停止时"也能成立、让 wait 返回，否则会死锁。
            cv_.wait(lock, [this] {
                return stop_ || !tasks_.empty();    // 醒来的条件：要停 或 有任务
            });

            // 要停 且 队列空 → 退出。
            //   && tasks_.empty() 的意义：
            //   让"轮到（被唤醒的）空闲线程"先去把 tasks_ 里剩下的任务执行完；
            //   只有当"所有任务都执行完"（队列空）时，才真正退出、归还线程。
            //   这样保证"停止请求"不会导致"队列里残留任务被丢弃"。
            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());   // 取一个任务（移动）
            tasks_.pop();
        }                                       // 出作用域，自动放锁

        task();     // 执行任务（不持锁）
    }
}