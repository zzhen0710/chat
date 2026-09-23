#include "client.h"

#include <iostream>   // std::cerr
#include <cstdlib>    // atoi

// 客户端入口
// 用法: ./chat_client <服务器IP> <昵称> [端口]
//   端口不传则用默认 CHAT_PORT
int main(int argc, char* argv[]) {
    // IP、昵称必须传
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <服务器IP> <昵称> [端口]\n";
        return -1;
    }

    std::string ip   = argv[1];
    std::string name = argv[2];
    int port = (argc > 3) ? atoi(argv[3]) : CHAT_PORT;

    try {
        ChatClient client(ip, name, port);   // 构造失败会 throw
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "客户端异常: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}