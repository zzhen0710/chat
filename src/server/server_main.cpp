#include "server.h"

#include <iostream>
#include <cstdlib>      // atoi

// 服务器入口
// 用法: ./chat_server [端口]  （不传则用默认 CHAT_PORT）
int main(int argc, char* argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : CHAT_PORT;

    try {
        ChatServer server(port);   // 构造失败会 throw
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "服务器异常: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}