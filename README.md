# 多人网络聊天系统（TCP + 线程池）

基于 TCP 的多人聊天室。服务器用线程池并发处理客户端，支持多客户端同时在线、
消息广播、上下线通知；客户端用 `poll` 单线程同时管理"终端输入"和"服务器消息"。

- **完成时间**：2026-09-24
- **当前版本**：Release 2.1

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

**服务器终端指令**：

| 指令 | 作用 |
|------|------|
| `count` | 显示当前在线数 |
| `list` | 显示在线数 + 每个用户明细（fd / 昵称 / IP / 端口） |
| `kick <name>` | 踢出指定昵称的用户 |
| `quit` / `exit` | 关闭服务器（广播"服务器已关闭"后退出） |

---

## 文件结构

```
chat/
├── CMakeLists.txt
├── README.md
├── chat_client         # 可执行文件（构建生成）
├── chat_server         # 可执行文件（构建生成）
├── include/
│   ├── common.h        # 协议 + 工具
│   ├── threadpool.h    # 线程池
│   ├── server.h        # ChatServer
│   └── client.h        # ChatClient
├── src/
│   ├── common.cpp
│   ├── threadpool.cpp  # 线程池实现（通用基础设施，独立）
│   ├── client/
│   │   ├── client_main.cpp
│   │   └── client.cpp
│   └── server/
│       ├── server_main.cpp
│       ├── server.cpp
│       ├── handle_client.cpp
│       └── cmd.cpp
└── build/              # CMake 中间文件（不提交 Git）
    ├── CMakeCache.txt
    ├── CMakeFiles/     # 各目标的 .o、依赖、构建规则
    ├── Makefile
    └── ...
```

> `build/` 为 CMake 构建目录，存放中间文件（`.o`、缓存、Makefile 等），
> 不属于源码，已在 `.gitignore` 中排除。
> `threadpool` 为通用基础设施，`.cpp` 从 `src/server/` 提回 `src/`（与 `common.cpp` 同级）。

---

## 文件业务

### common：双方共用的"协议 + 工具"

- **常量**：`CHAT_PORT`（端口）、`NAME_SIZE`（昵称长度）、`TEXT_SIZE`（正文长度）
- **消息结构 `Msg`**：`type` + `name` + `text`，含 `serialize()` / `deserialize()`
- **收发函数**：`send_msg` / `recv_msg`（对外，序列化后整条收发）
  - 内部 `send_all` / `recv_all`（循环收发，处理 TCP"不一定一次发完/收全"）
- **日志宏**：`LOG`（信息）、`ERR_LOG`（错误，`perror` + 位置）

> `send_all` / `recv_all` 仅在 `common.cpp` 内部使用（`static`），对外只暴露 `send_msg` / `recv_msg`。

### threadpool：线程池（通用基础设施）

- 固定数量工作线程 + 任务队列，生产者-消费者模型
- 对外：构造、`add_task`、`size`；内部：`worker`

### server：聊天服务器

| 文件 | 业务 |
|------|------|
| `server_main.cpp` | 程序入口：解析端口/线程数，创建并运行 `ChatServer` |
| `server.cpp` | 构造（socket/bind/listen）、析构、`run`（poll + accept）、增删读在线表、广播 |
| `handle_client.cpp` | `handleClient`：一个客户端的完整生命周期（登录 → 收发 → 下线） |
| `cmd.cpp` | 服务器终端指令：`handleCmd` / `listClients` / `kickClient` |

### client：聊天客户端

| 文件 | 业务 |
|------|------|
| `client_main.cpp` | 程序入口：解析 IP/昵称/端口，创建并运行 `ChatClient` |
| `client.cpp` | 连接、`sendMsg`、`poll` 主循环（收发）、消息打印、退出 |

---

## 通信逻辑

**统一协议**：一条消息 = 定长 `Msg`（`type` + `name` + `text`），
`serialize` 成字节流后经 `send_msg` / `recv_msg` 整条收发。

### 客户端

1. `socket` + `connect`（失败 `throw`）
2. 发 `MSG_LOGIN`（带昵称）
3. **`poll` 主循环**（单线程，同时管两个 fd）：
   - **终端可读** → `getline` 读一行 → 发 `MSG_CHAT`
   - **服务器可读** → `recv_msg` 收一条 → `printMsg` 打印
4. **退出**：输 `quit` 发 `MSG_QUIT`；收到 `MSG_KICK`/`MSG_SHUTDOWN` 则退；连接断退出

### 服务器

1. 构造：`socket` → `setsockopt(SO_REUSEADDR)` → `bind` → `listen`
2. **`run`（poll 管"终端 + 监听 socket"）**：
   - 终端可读 → `handleCmd`（处理指令）
   - 监听可读 → `accept` → `pool_.add_task(handleClient)`
3. **`handleClient`（线程池里跑，一个客户端的生命周期）**：
   - 收 `LOGIN`（拿昵称）
   - `acceptClient`：**满（在线数 ≥ 线程数）→ `MSG_REJECT`；重名 → `MSG_DUPNAME`**，均 `close` 拒绝
   - 通过 → 加入在线表、广播 `MSG_ONLINE`（排除自己）
   - 循环 `recv_msg`：`MSG_CHAT` → 广播给别人；`MSG_QUIT` → 退出
   - 下线：移除 + `close` + 广播 `MSG_OFFLINE`
4. **在线客户端**：`unordered_map<int, Client>`（`fd → {fd, name, addr}`）+ `mutex` 保护
5. **广播 `broadcast(msg, except_fd)`**：遍历在线表、跳过 `except_fd`、`send_msg`

---

## 消息类型（枚举）

| 值 | 类型 | 方向 | 说明 |
|----|------|------|------|
| 1 | `MSG_LOGIN` | 客户端→服务器 | 登录（带昵称） |
| 2 | `MSG_CHAT` | 双向 | 普通聊天 |
| 3 | `MSG_QUIT` | 客户端→服务器 | 客户端主动退出 |
| 4 | `MSG_ONLINE` | 服务器→客户端 | 系统：上线通知 |
| 5 | `MSG_OFFLINE` | 服务器→客户端 | 系统：下线通知 |
| 6 | `MSG_KICK` | 服务器→客户端 | 系统：被踢（客户端收到后退出） |
| 7 | `MSG_DUPNAME` | 服务器→客户端 | 系统：昵称重复 |
| 8 | `MSG_REJECT` | 服务器→客户端 | 系统：服务器满、拒绝 |
| 9 | `MSG_SHUTDOWN` | 服务器→客户端 | 系统：服务器关闭 |

---

## 技术栈

- **语言 / 标准**：C++11
- **网络**：TCP socket（`socket` / `bind` / `listen` / `accept` / `recv` / `send`）
- **并发**：`std::thread`、`std::mutex`、`std::condition_variable`、线程池（生产者-消费者）
- **I/O 多路复用**：`poll`（客户端、服务器主循环）
- **语法 / 特性**：`std::function` / lambda（任务）、`unordered_map`（在线表）、
  `enum : uint8_t`（消息类型）、RAII、`= delete`、`explicit`、结构化绑定
- **构建**：CMake

---

## 版本历史

| 版本 | 提交 | 说明 |
|------|------|------|
| **Release 2.1** | `d29a3dc` | 优化文件结构：`threadpool.cpp` 从 `src/server/` 提回 `src/`（通用基础设施独立） |
| **Release 2.0** | `c6acc09` | 服务器控制指令（`count`/`list`/`kick`/`quit`）、满则拒绝、重名通知、文件拆分 |
| **Release 1.0** | `fc71cbc` | 客户端 `poll` 收发 + 优雅退出；服务器线程池 + 广播 + 上下线通知 |
| init | `d536b56` | 项目结构（include/src/docs） |