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
    int client_count_ = 0;   // 在线客户端数（和 clients_ 同锁保护）

    std::unordered_map<int, Client> clients_;   // 在线客户端（fd → Client）
    std::mutex clients_mtx_;                    // 保护 clients_

    // 增删读（都持同锁操作，注意不能嵌套防止死锁）
    bool addClient(int fd, const struct sockaddr_in& addr, const char* name);
    void removeClient(int fd);
    int getClientCount();

    // 业务
    void handleClient(int fd, const struct sockaddr_in& addr); // 线程池里跑：一个客户端的生命周期
    void broadcast(const Msg& msg, int except_fd = -1);        // 广播（except_fd 除外）
    // 评估是否接收新客户端：满/重名 → 拒绝（发消息 + 关），返回 false
    bool acceptClient(int fd, const struct sockaddr_in& addr, const char* name);
    
    // 指令
    void handleCmd(const std::string& line);   // 终端指令
    void kickClient(const std::string& name);  // 踢人
    void listClients();  // 显示所有在线用户
};

#endif