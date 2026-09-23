# 多人网络聊天系统（TCP + 线程池）

基于 TCP 的多人聊天室。服务器用线程池并发处理客户端，支持多客户端同时在线、消息广播、上下线通知。客户端用 `poll` 单线程同时管理"终端输入"和"服务器消息"。

## 编译

```bash
cmake -B build -S .
cmake --build build
```

生成两个可执行文件：`chat_server`、`chat_client`。

## 运行

```bash
# 服务器：端口 线程数（都可省，默认 8964 / 4）
./chat_server 8964 4

# 客户端：IP 昵称 [端口]
./chat_client 127.0.0.1 张三
```

可开多个客户端，互相发消息。

---

## common：双方共用的"协议 + 工具"

`common.h` / `common.cpp` 存"服务器和客户端都要用的东西"：

- **常量**：`CHAT_PORT`（端口）、`NAME_SIZE`（昵称长度）、`TEXT_SIZE`（正文长度）
- **消息类型 `MsgType`**：
  - 用户消息：`MSG_LOGIN`（登录）、`MSG_CHAT`（聊天）、`MSG_QUIT`（退出）
  - 系统消息：`MSG_ONLINE`（上线）、`MSG_OFFLINE`（下线）、`MSG_KICK`（被踢）、`MSG_DUPNAME`（昵称重复）
- **消息结构 `Msg`**：`type` + `name` + `text`，含 `serialize()` / `deserialize()`
- **收发函数**：
  - `send_msg` / `recv_msg`（对外：序列化后整条收发）
  - 内部 `send_all` / `recv_all`（循环收发，处理 TCP"不一定一次发完/收全"）
- **日志宏**：`LOG`（信息）、`ERR_LOG`（错误，`perror` + 位置）

> 注：`send_all` / `recv_all` 仅在 `common.cpp` 内部使用（`static`），对外只暴露 `send_msg` / `recv_msg`。

---

## 客户端逻辑

**流程**：连接 → 发 LOGIN → `poll` 收发 → 退出。

1. **构造**：`socket` + `connect`（失败 `throw`）
2. **发 `LOGIN`**：把昵称告诉服务器
3. **`poll` 主循环**（单线程，同时管两个 fd）：
   - **终端可读** → `getline` 读一行 → 发 `MSG_CHAT`
   - **服务器可读** → `recv_msg` 收一条 → `printMsg` 打印
4. **退出**：
   - 输 `quit` → 发 `MSG_QUIT` → 退
   - 收到 `MSG_KICK`（被踢）→ 打印后退出
   - 连接断 / `poll` 失败 → 退出
5. **`printMsg`**（按 `type` 分）：
   - `MSG_CHAT` → `name: text`
   - `MSG_ONLINE` / `MSG_OFFLINE` / `MSG_DUPNAME` → `>>> text`
   - `MSG_KICK` → `>>> text` 且退出

**为什么用 `poll`**：单线程同时管"终端 + 服务器"，实时、不用子线程、不用锁。

---

## 服务器逻辑

**流程**：监听 → accept → 线程池 → 每个客户端一个任务。

1. **构造**：`socket` → `setsockopt(SO_REUSEADDR)` → `bind` → `listen`（失败 `throw`）
2. **`run` 主循环**：
   - `accept` 新连接 → `pool_.add_task(handleClient)`（线程池处理）
3. **`handleClient`（线程池里跑，一个客户端的生命周期）**：
   - 收 `LOGIN`（拿昵称）
   - `addClient`：昵称查重，加入 `clients_`
   - 广播 `MSG_ONLINE`（排除自己）
   - 循环 `recv_msg`：
     - `MSG_CHAT` → `broadcast`（广播给别人）
     - `MSG_QUIT` → 退出
   - 下线：`removeClient` + `close` + 广播 `MSG_OFFLINE`
4. **在线客户端**：`unordered_map<int, Client>`（`fd → {fd, name, addr}`）+ `mutex` 保护
5. **广播 `broadcast(msg, except_fd)`**：遍历 `clients_`、跳过 `except_fd`、`send_msg`

**并发**：线程池（生产者-消费者）处理多客户端；`clients_` 多线程读写加 `mutex`。

---

## 文件结构

```
chat/
├── README.md
├── CMakeLists.txt
├── include/
│   ├── common.h        # 协议 + 工具
│   ├── threadpool.h    # 线程池
│   ├── server.h        # ChatServer
│   └── client.h        # ChatClient
├── src/
│   ├── common.cpp
│   ├── server/
│   │   ├── server_main.cpp
│   │   ├── server.cpp
│   │   ├── handle_client.cpp
│   │   └── threadpool.cpp
│   └── client/
│       ├── client_main.cpp
│       └── client.cpp
└── build/
```

## 技术栈

- **语言**：C++11
- **网络**：TCP socket（`socket` / `bind` / `listen` / `accept` / `recv` / `send`）
- **并发**：`std::thread`、`std::mutex`、`std::condition_variable`、线程池
- **I/O**：`poll`（客户端多路复用）
- **其他**：`std::function` / lambda（任务）、`unordered_map`（在线表）、RAII、`= delete` / `explicit`