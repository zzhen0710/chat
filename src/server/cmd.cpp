#include "server.h"

#include <arpa/inet.h>   // inet_ntop
#include <cstdlib>       // exit
#include <unistd.h>      // close

// 处理服务器终端指令
void ChatServer::handleCmd(const std::string& line) {
    if (line == "quit" || line == "exit") {
        LOG("[server] shutting down...");
        exit(0);                                  // 直接退出进程

    } else if (line == "count") {
        LOG("[server] online = %d", getClientCount());   // 只报在线数

    } else if (line == "list") {
        listClients();                            // 报在线数 + 每人明细

    } else if (line.find("kick ") == 0) {         // 以 "kick " 开头（后面是昵称）
        kickClient(line.substr(5));               // 取 "kick " 之后的昵称

    } else if (!line.empty()) {                   // 以上都不是，未知命令
        LOG("[server] unknown cmd: %s", line.c_str());
    }
}

// 显示所有在线用户
void ChatServer::listClients() {
    std::lock_guard<std::mutex> lock(clients_mtx_);   // 加锁遍历

    LOG("[server] online = %d", client_count_);
    for (const auto& [fd, cli] : clients_) {          // 结构化绑定
        char ip[INET_ADDRSTRLEN];
        // 线程安全：结果写进自己的 ip，不用 inet_ntoa（它用全局静态缓冲区、多线程会互相覆盖）
        inet_ntop(AF_INET, &cli.addr.sin_addr, ip, sizeof(ip));
        LOG("fd = %d name = %s ip = %s port = %d",
            fd, cli.name, ip, ntohs(cli.addr.sin_port));
    }
}

// 踢一个客户端（按昵称）：通知本人 + 广播别人
void ChatServer::kickClient(const std::string& name) {
    int  target_fd = -1;
    char target_name[NAME_SIZE] = "";

    // 1. 锁内只找目标 fd（不在锁内 send，避免 broadcast 再拿同锁死锁）
    {
        std::lock_guard<std::mutex> lock(clients_mtx_);
        for (const auto& [fd, cli] : clients_) {
            if (name == cli.name) {
                target_fd = fd;
                snprintf(target_name, sizeof(target_name), "%s", cli.name);
                break;
            }
        }
    }
    if (target_fd == -1) {
        LOG("[server] kick: [%s] not found", name.c_str());
        return;
    }

    // 2. 通知本人（MSG_KICK → 客户端收到后退出）—— 锁外发
    Msg kick{};
    kick.type = MSG_KICK;
    snprintf(kick.text, sizeof(kick.text), "你已被服务器踢出!");
    send_msg(target_fd, kick);

    // 3. 广播给别人（MSG_OFFLINE 系统文本）—— 锁外发（broadcast 内部自己加锁）
    Msg notice{};
    notice.type = MSG_OFFLINE;
    snprintf(notice.text, sizeof(notice.text), "用户-%s 已被踢出", target_name);
    broadcast(notice, target_fd);              // 排除被踢者自己

    LOG("[server] kicked [%s] fd = %d", target_name, target_fd);
}