#include "server.h"

#include <unistd.h>       // close
#include <arpa/inet.h>    // inet_ntoa
#include <poll.h>         // poll
#include <iostream>       // std::cin / std::getline

// 服务器主循环：poll 同时管"终端指令 + 新连接"
void ChatServer::run() {
    LOG("[server] listening on port %d, waiting...", port_);

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;   // 终端
    fds[0].events = POLLIN;
    fds[1].fd = sock_fd_;       // 监听 socket
    fds[1].events = POLLIN;

    while (true) {
        fds[0].revents = fds[1].revents = 0;   // 清空上次就绪标志
        int n = poll(fds, 2, -1);              // 阻塞，等任一可读
        if (n < 0) {
            if (errno == EINTR) continue;      // 被信号打断，重试
            ERR_LOG("poll error");
            break;
        }

        // 终端可读 → 读一行、处理指令
        if (fds[0].revents & POLLIN) {
            std::string line;
            if (!std::getline(std::cin, line)) break;   // EOF
            handleCmd(line);
        }

        // 监听 socket 可读 → accept、交给线程池
        if (fds[1].revents & POLLIN) {
            struct sockaddr_in cli_addr{};
            socklen_t cli_len = sizeof(cli_addr);

            int fd = accept(sock_fd_, (struct sockaddr*)&cli_addr, &cli_len);
            if (fd == -1) {
                if (errno == EINTR) continue;
                ERR_LOG("accept error");
                continue;
            }

            // 在线数 >= 线程数 → 拒绝（发 MSG_REJECT、关连接、不处理）
            if (getClientCount() >= (int)pool_.size()) {
                LOG("[server] reject(full) | ip=%s port=%d fd=%d | online=%d/%d",
                    inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port),
                    fd, getClientCount(), (int)pool_.size());

                Msg busy{};
                busy.type = MSG_REJECT;
                snprintf(busy.text, sizeof(busy.text), "服务器繁忙，请稍后再试");
                send_msg(fd, busy);
                close(fd);
                continue;
            }

            LOG("[server] accept | ip = %s port = %d fd = %d",
                inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), fd);

            // 每个连接一个任务
            pool_.add_task([this, fd, cli_addr] { handleClient(fd, cli_addr); });
        }
    }
}
// 构造：socket → setsockopt → bind → listen；失败抛异常
ChatServer::ChatServer(int port, int thread_num)
    : sock_fd_(-1), port_(port), pool_(thread_num)   // 线程池 4 个线程
{
    // 1. 创建 TCP 套接字
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ == -1) {
        throw std::runtime_error(std::string("socket 失败: ") + strerror(errno));
    }

    // 2. 端口复用（避免重启时 "Address already in use"）
    int opt = 1;
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        close(sock_fd_);
        throw std::runtime_error(std::string("setsockopt 失败: ") + strerror(errno));
    }

    // 3. 服务器地址：绑定本机所有网卡 + 指定端口
    struct sockaddr_in sin{
        AF_INET,
        htons(port),
        { INADDR_ANY }
    };

    // 4. 绑定
    if (bind(sock_fd_, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
        close(sock_fd_);
        throw std::runtime_error(std::string("bind 失败: ") + strerror(errno));
    }

    // 5. 监听（backlog = 128）
    if (listen(sock_fd_, 128) == -1) {
        close(sock_fd_);
        throw std::runtime_error(std::string("listen 失败: ") + strerror(errno));
    }

    LOG("[server] init success, listening on port %d.", port);
}

// 析构：关闭监听套接字
ChatServer::~ChatServer() {
    if (sock_fd_ >= 0) close(sock_fd_);
}

// 加入在线表：先查重（内联），重复返回 false
bool ChatServer::addClient(int fd, const struct sockaddr_in& addr, const char* name) {
    std::lock_guard<std::mutex> lock(clients_mtx_);

    // 查重
    for (const auto& [f, cli] : clients_) {
        if (strcmp(cli.name, name) == 0) return false;
    }

    Client cli{fd, {}, addr};
    snprintf(cli.name, sizeof(cli.name), "%s", name);
    clients_[fd] = cli;
    ++client_count_;        // 计数 +1

    return true;
}

// 移出在线表：获取锁后直接按 fd 删去
void ChatServer::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mtx_);
    clients_.erase(fd);
    --client_count_;            // 计数 -1
}

// 持锁读在线数：和 clients_ 同一把锁，保证读到一致值
int ChatServer::getClientCount() {
    std::lock_guard<std::mutex> lock(clients_mtx_);
    return client_count_;
}

// 广播给所有在线客户端（except_fd 除外）
void ChatServer::broadcast(const Msg& msg, int except_fd) {
    std::lock_guard<std::mutex> lock(clients_mtx_);

    for (const auto& [fd, cli] : clients_) {
        if (fd == except_fd) continue;      // 跳过发起者
        if (send_msg(fd, msg) != 0) {
            LOG("广播失败, fd = %d", fd);     // 该客户端可能已断
        }
    }
}