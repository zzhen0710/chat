#ifndef _SERVER_H_
#define _SERVER_H_

#include "common.h"
#include "threadpool.h"

#include <unordered_map>
#include <netinet/in.h>   // sockaddr_in

// 在线客户端信息
struct Client {
    int fd;                     // 套接字
    char name[NAME_SIZE];       // 昵称
    struct sockaddr_in addr;    // 客户端地址
};

// 聊天服务器：监听、accept、线程池处理、广播
class ChatServer {
public:
    explicit ChatServer(int port = CHAT_PORT, int thread_num = 4);  // 构造，默认使用协议端口，4线程
    ~ChatServer();

    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    void run();   // accept 循环

private:
    int sock_fd_;           // 监听 socket
    int port_;              // 端口
    ThreadPool pool_;       // 线程池

    std::unordered_map<int, Client> clients_;   // 在线客户端（fd → Client）
    std::mutex clients_mtx_;                    // 保护 clients_

    // 增删（都加锁）
    bool addClient(int fd, const struct sockaddr_in& addr, const char* name);
    void removeClient(int fd);

    // 业务
    void handleClient(int fd, const struct sockaddr_in& addr);   // 线程池里跑：一个客户端的生命周期
    void broadcast(const Msg& msg, int except_fd = -1);          // 广播（except_fd 除外）
};

#endif