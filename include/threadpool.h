#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <vector>               // 向量管理线程容器
#include <queue>                // 队列 FIFO 管理任务
#include <thread>               // 支持线程创建及其他操作
#include <mutex>                // 互斥锁保护线程分配任务等临界区操作
#include <condition_variable>   // 条件变量，有任务时唤醒线程
#include <functional>           // 封装可调用函数

class ThreadPool {
public:
    using Task = std::function<void()>;                 // 任务类型：无参、返回 void

    explicit ThreadPool(size_t n = 4);                  // 构造：默认线程池 4 线程
    ~ThreadPool();                                      // 析构：停止、唤醒、释放线程资源

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void add_task(Task task);                           // 添加任务到任务队列

private:
    void worker();                          // 工作线程本身：循环利用 cv_ 监测并取任务、执行

    std::vector<std::thread> workers_;      // 工作线程
    std::queue<Task> tasks_;                // 任务队列
    std::mutex mtx_;                        // 保护临界区资源（tasks_ / stop_）
    std::condition_variable cv_;            // 通知：有新任务 / 要停止
    bool stop_;                             // 线程池停止标志（true = 该停了）
};

#endif