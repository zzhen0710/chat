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

    // 2. 加入在线表（先昵称查重）
    if (!addClient(fd, addr, msg.name)) {
        LOG("[server] duplicate name rejected | ip = %s port = %d fd = %d name = %s",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fd, msg.name);
        close(fd);
        return;
    }
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