# 最低 CMake 版本
cmake_minimum_required(VERSION 3.0)

# 项目名
project(chat)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)

# 头文件搜索路径（src 里 #include "xxx.h" 能找到 include/ 下的头）
include_directories(${CMAKE_SOURCE_DIR}/include)

# ============ 服务器可执行文件 ============
# main + 服务器  + 线程池
add_executable(chat_server
    src/server/server_main.cpp
    src/server/server.cpp
    src/server/cmd.cpp
    src/server/handle_client.cpp
    src/server/threadpool.cpp
    src/common.cpp
)

# ============ 客户端可执行文件 ============
# main + 客户端
add_executable(chat_client
    src/client/client_main.cpp
    src/client/client.cpp
    src/common.cpp
)

# 找线程库（std::thread 需要）
find_package(Threads REQUIRED)
target_link_libraries(chat_server Threads::Threads)
target_link_libraries(chat_client Threads::Threads)

# 两个可执行文件都生成到项目根目录
set_target_properties(chat_server chat_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)