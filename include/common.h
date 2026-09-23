#ifndef _COMMON_H_
#define _COMMON_H_

#include <cstdio>       // printf / fprintf / perror
#include <cstring>      // memcpy / strlen
#include <cstdint>      // uint8_t
#include <string>       // std::string

//  协议常量（双方约定）
#define CHAT_PORT   8964          // 服务器端口
#define NAME_SIZE   32            // 昵称最大长度
#define TEXT_SIZE   1024          // 消息正文最大长度

//  消息类型（协议）
enum MsgType : uint8_t {
    MSG_LOGIN    = 1,   // 登录
    MSG_CHAT     = 2,   // 普通聊天
    MSG_QUIT     = 3,   // 退出
    MSG_ONLINE   = 4,   // 系统：上线通知
    MSG_OFFLINE  = 5,   // 系统：下线通知
    MSG_KICK     = 6,   // 系统：被踢
    MSG_DUPNAME  = 7,   // 系统：昵称重复
};

//  消息结构体（定长）
struct Msg {
    uint8_t type;          // 细类（按 category 解释）
    char name[NAME_SIZE];   // 发送者昵称
    char text[TEXT_SIZE];   // 消息正文

    std::string serialize() const;              // 结构体 → 字节流
    void deserialize(const std::string& data);  // 字节流 → 结构体
};

//  消息收发（协议级，双方都用）
// 发一条 MSG（内部：serialize + send_all）
// 成功返回 0，失败返回 -1
int send_msg(int fd, const Msg& msg);

// 收一条 MSG（内部：recv_all + deserialize）
// 成功返回 0，失败返回 -1
int recv_msg(int fd, Msg& msg);

//  日志宏
#define DEBUG 1
#if DEBUG
    #define LOG(fmt, ...) do { \
        printf("[LOG] " fmt "\n", ##__VA_ARGS__); \
    } while(0)
#else
    #define LOG(fmt, ...) do {} while(0)    // 不直接为空防止 if(); else 下 else 配错
#endif

// errno 只在"系统调用失败、且紧跟其后"时有效：成功后不改、中间插别的调用会被覆盖；
// 故 ERR_LOG 必须紧跟在失败判断后调用（用法错误/C++异常等非系统调用场景不适用）。
#define ERR_LOG(msg) do { \
    perror(msg); \
    fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, __func__); \
} while(0)

#endif