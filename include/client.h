#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "common.h"

// ============================================================
//  聊天客户端：连接服务器、收发消息
// ============================================================
class ChatClient {
private:
    int         sock_fd_;       // 客户端套接字 fd
    std::string server_ip_;     // 服务器 IP
    int         server_port_;   // 服务器端口
    std::string name_;          // 昵称

    // 发一条消息（返回 0 成功 / -1 失败）
    int sendMsg(MsgType type, const std::string& text = "");

public:
    // 构造：连接服务器（失败 throw）
    ChatClient(const std::string& ip, const std::string& name,
               int port = CHAT_PORT);
    ~ChatClient();

    // 禁止拷贝（独占一个 socket）
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void run();   // 主循环：读键盘、发消息
};

#endif