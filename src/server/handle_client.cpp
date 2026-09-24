#include "server.h"

#include <unistd.h>       // close
#include <arpa/inet.h>    // inet_ntoa

// 处理一个客户端（线程池里跑）
// 流程：登录 → 广播上线 → 循环收（CHAT 广播 / QUIT 退）→ 下线 + 广播
void ChatServer::handleClient(int fd, const struct sockaddr_in& addr) {
    Msg msg{};
    // 1. 先收 LOGIN（拿昵称）
    if (recv_msg(fd, msg) != 0) {          // ← 收！
        close(fd);
        return;
    }

    // 2. 加入在线表
    // 先评估是否接收（满 / 重名 → 拒，内部已发消息 + close）
    if (!acceptClient(fd, addr, msg.name)) return;
    LOG("[server] user online | ip = %s port = %d fd = %d name = %s | online = %d",
        inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name, getClientCount());

    // 3. 广播"上线"（排除上线的用户自己）
    Msg notice{};
    notice.type = MSG_ONLINE;   // 通过 type 判断类型，name 为空串
    snprintf(notice.text, sizeof(notice.text), "用户-%s 上线了!", msg.name);
    broadcast(notice, fd);

    // 4. 循环收消息
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

    // 5. 下线：从在线列表移除 + 关闭套接字 + 广播信息
    removeClient(fd);
    close(fd);
    LOG("[server] user offline | ip = %s port = %d fd = %d name = %s | online = %d",
        inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name, getClientCount());

    Msg bye{};
    bye.type = MSG_OFFLINE;
    snprintf(bye.text, sizeof(bye.text), "用户-%s 已下线", msg.name);
    
    broadcast(bye);
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

// 评估是否接收：满则拒（MSG_REJECT）、重名则拒（MSG_DUPNAME）；通过返回 true
bool ChatServer::acceptClient(int fd, const struct sockaddr_in& addr, const char* name) {
    // 1. 在线数 >= 线程数 → 拒
    if (getClientCount() >= (int)pool_.size()) {
        LOG("[server] reject(full) | ip = %s port = %d fd = %d | online = %d / %d",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port),
            fd, getClientCount(), (int)pool_.size());

        Msg busy{};
        busy.type = MSG_REJECT;
        snprintf(busy.text, sizeof(busy.text), "服务器繁忙, 请稍后再试.");
        send_msg(fd, busy);
        close(fd);
        return false;
    }

    // 2. 昵称查重 + 最终同意加入在线表
    if (!addClient(fd, addr, name)) {
        LOG("[server] duplicate name rejected | ip = %s port = %d fd = %d name = %s",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, name);

        Msg dup{};
        dup.type = MSG_DUPNAME;
        snprintf(dup.text, sizeof(dup.text), "昵称 [%s] 已被占用, 请换一个.", name);
        send_msg(fd, dup);
        close(fd);
        return false;
    }

    return true;   // 接收
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