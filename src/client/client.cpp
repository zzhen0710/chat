#include "client.h"

#include <iostream>       // std::cin / std::cout
#include <sys/socket.h>   // socket / connect / shutdown
#include <netinet/in.h>   // sockaddr_in
#include <arpa/inet.h>    // inet_addr
#include <unistd.h>       // close
#include <stdexcept>      // std::runtime_error
#include <poll.h>         // poll

// 打印消息；返回"是否要退出"（QUIT / KICK → 退）
static bool printMsg(const Msg& msg) {
    switch (msg.type) {
        case MSG_LOGIN:
            std::cout << ">>> 登录成功" << std::endl;   // 或忽略
            break;

        case MSG_CHAT:
            std::cout << msg.name << ": " << msg.text << std::endl;
            break;

        case MSG_ONLINE:
        case MSG_OFFLINE:
        case MSG_DUPNAME:
            std::cout << ">>> " << msg.text << std::endl;   // "用户-xxx 上 / 下线了"
            break;

        case MSG_QUIT:
        case MSG_KICK:
            std::cout << ">>> " << msg.text << std::endl;   // "你已被踢出"
            return true;    // ← 要退出

        default:
            break;
    }
    return false;   // ← 不退出
}

// 构造：连接服务器（失败 throw）
ChatClient::ChatClient(const std::string& ip, const std::string& name, int port)
    : sock_fd_(-1), server_ip_(ip), server_port_(port), name_(name)
{
    // 1. 创建 TCP 套接字
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ == -1) {
        throw std::runtime_error(std::string("socket 失败: ") + strerror(errno));
    }

    // 2. 服务器地址（列表初始化）
    struct sockaddr_in sin{
        AF_INET,
        htons(port),
        { inet_addr(ip.c_str()) }
    };

    // 3. 连接
    if (connect(sock_fd_, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
        close(sock_fd_);   // 先关，再抛（否则 fd 泄漏）
        throw std::runtime_error(std::string("connect 失败: ") + strerror(errno));
    }

    LOG("已连接服务器 %s : %d.", ip.c_str(), port);
}

// 析构：关闭套接字
ChatClient::~ChatClient() {
    if (sock_fd_ >= 0) {
        close(sock_fd_);
    }
}

// 发一条消息（返回 0 成功 / -1 失败）
int ChatClient::sendMsg(MsgType type, const std::string& text) {
    Msg msg;
    msg.type = type;

    // 填昵称、正文：snprintf 保证 '\0' 结尾、不越界
    snprintf(msg.name, sizeof(msg.name), "%s", name_.c_str());
    snprintf(msg.text, sizeof(msg.text), "%s", text.c_str());

    if (send_msg(sock_fd_, msg) != 0) {
        ERR_LOG("send_msg error");
        return -1;
    }
    return 0;
}

// 主循环：poll 同时管"终端输入 + 服务器消息"，单线程、实时
void ChatClient::run() {
    // 1. 发 LOGIN
    if (sendMsg(MSG_LOGIN) != 0) {
        LOG("登录失败.");
        return;
    }

    // 2. poll 监视两个 fd：stdin（终端）+ sock（服务器）
    struct pollfd fds[2];
    fds[0].fd     = STDIN_FILENO;   // 终端
    fds[0].events = POLLIN;
    fds[1].fd     = sock_fd_;       // 服务器
    fds[1].events = POLLIN;

    while (true) {
        fds[0].revents = fds[1].revents = 0;    // 清空上次的就绪标志，poll 会重新填写
        int n = poll(fds, 2, -1);   // 阻塞，等任一 fd 可读
        if (n < 0) {
            if (errno == EINTR) continue;   // 被信号打断，重试
            ERR_LOG("poll error");
            break;
        }

        // 终端可读 → 读一行、发送
        if (fds[0].revents & POLLIN) {
            std::string line;
            if (!std::getline(std::cin, line)) break;   // 输入结束(EOF)

            if (line == "quit") {
                sendMsg(MSG_QUIT);
                LOG("你已退出.");
                break;
            }
            if (!line.empty() && sendMsg(MSG_CHAT, line) != 0) {    // 发送出现问题，再发也发不出去，直接 LOG 后退出
                LOG("发送失败, 退出.");
                break;
            }
        }

        // 服务器可读 → 收一条、处理
        if (fds[1].revents & POLLIN) {
            Msg msg;
            if (recv_msg(sock_fd_, msg) != 0) {
                LOG("与服务器断开.");   // 连接断
                break;
            }

            if (printMsg(msg)) {   // 返回 true = QUIT/KICK → 退
                break;
            }
        }
    }
    LOG("客户端退出");
}