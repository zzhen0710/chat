#include "common.h"

#include <sys/types.h>
#include <sys/socket.h>

// ============================================================
//  内部工具（仅本文件用，static）
// ============================================================

// 发"完整 len 字节"（TCP 不保证一次发完，循环发）
// 成功返回 len，失败返回 -1
static int send_all(int fd, const char* buf, int len) 
{
    int sent = 0;
    while (sent < len) {
        int n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;      // 出错 / 对端关闭
        sent += n;
    }
    return len;
}

// 收"完整 len 字节"（TCP 不保证一次收全，循环收）
// 成功返回 len，失败返回 -1
static int recv_all(int fd, char* buf, int len) 
{
    int recvd = 0;
    while (recvd < len) {
        int n = recv(fd, buf + recvd, len - recvd, 0);
        if (n <= 0) return -1;      // 出错 / 对端关闭
        recvd += n;
    }
    return len;
}

// ============================================================
//  Msg 序列化 / 反序列化
// ============================================================

// 序列化：结构体 → 字节流
std::string Msg::serialize() const
{
    std::string data;
    data.append(reinterpret_cast<const char*>(&type), sizeof(type));    // 强转类型
    data.append(name, sizeof(name));
    data.append(text, sizeof(text));
    
    return data;
}

// 反序列化：字节流 → 结构体
void Msg::deserialize(const std::string& data)
{
    size_t offset = 0;  // 全过程偏移量
    
    memcpy(&type, data.c_str() + offset, sizeof(type));
    offset += sizeof(type);

    memcpy(&name, data.c_str() + offset, sizeof(name));
    offset += sizeof(name);

    memcpy(&text, data.c_str() + offset, sizeof(text));
}

// ============================================================
//  对外：收发一条 Msg
// ============================================================

// 发一条 Msg（内部：serialize + send_all）
// 成功返回 0，失败返回 -1
int send_msg(int fd, const Msg& msg)
{
    std::string data = msg.serialize();
    int n = send_all(fd, data.c_str(), data.size());
    return (n == (int)data.size()) ? 0 : -1;
}

// 收一条 Msg（内部：recv_all + deserialize）
// 成功返回 0，失败返回 -1
int recv_msg(int fd, Msg& msg)
{
    char buf[sizeof(Msg)];
    int n = recv_all(fd, buf, sizeof(buf));
    if (n != sizeof(buf)) return -1;

    msg.deserialize(std::string(buf, sizeof(Msg)));
    return 0;
}