#include "server.h"

#include <sys/socket.h>   // send / recv
#include <unistd.h>       // close

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

    return true;
}

// 移出在线表：获取锁后直接按 fd 删去
void ChatServer::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mtx_);
    clients_.erase(fd);
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

// 处理一个客户端（线程池里跑）
// 流程：登录 → 广播上线 → 循环收（CHAT 广播 / QUIT 退）→ 下线 + 广播
void ChatServer::handleClient(int fd, const struct sockaddr_in& addr) {
    Msg msg;

    // 1. 登录，加入在线表（先昵称查重）
    if (!addClient(fd, addr, msg.name)) {
        LOG("[server] duplicate name rejected | ip = %s port = %d fd = %d name = %s",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name);
        close(fd);
        return;
    }
    LOG("[server] user online | ip = %s port = %d fd = %d name = %s ",
        inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name);

    // 2. 广播"上线"（排除上线的用户自己）
    Msg notice;
    notice.type = MSG_ONLINE;   // 通过 type 判断类型，name 为空串
    snprintf(notice.text, sizeof(notice.text), "用户-%s 上线了!", msg.name);
    broadcast(notice, fd);

    // 3. 循环收消息
    while (true) {
        if (recv_msg(fd, msg) != 0) {
            LOG("[server] user disconnected | ip = %s port = %d fd = %d name = %s",
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name);
            break;
        }

        if (msg.type == MSG_CHAT) {
            broadcast(msg, fd);        // 聊天 → 广播给别人
        } else if (msg.type == MSG_QUIT) {
            break;                     // 主动退出
        }
    }

    // 4. 下线：从在线列表移除 + 关闭套接字 + 广播信息
    removeClient(fd);
    close(fd);

    Msg bye;
    bye.type = MSG_OFFLINE;
    // 下线
    LOG("[server] user offline | ip = %s port = %d fd = %d name = %s",
        inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name);
    broadcast(bye);
}

// 服务器主循环：accept → 丢给线程池处理
void ChatServer::run() {
    LOG("[server] listening on port %d, waiting...", port_);

    while (true) {
        struct sockaddr_in cli_addr{};
        socklen_t cli_len = sizeof(cli_addr);

        // 阻塞 accept
        int fd = accept(sock_fd_, (struct sockaddr*)&cli_addr, &cli_len);
        if (fd == -1) {
            if (errno == EINTR) continue;   // 被打断，重试
            ERR_LOG("accept error");
            continue;
        }

        LOG("[server] accept | ip = %s port = %d fd = %d",
            inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), fd);

        // 交给线程池：每个客户端一个任务，绑定参数后线程自启动
        pool_.add_task([this, fd, cli_addr] {
            handleClient(fd, cli_addr);
        });
    }
}