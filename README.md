# httpccc: 高性能 C++ HTTP 服务器



## 概述



`httpccc` 是一个基于现代 C++23 标准和 Linux Epoll 技术的**多线程、高性能** HTTP 服务器项目。本项目旨在提供一个稳健的网络基础框架，采用 **主线程 Reactor + 工作线程池 (Main Reactor + ThreadPool)** 架构，通过 `eventfd` 实现 I/O 线程与业务逻辑线程的解耦，实现了高效的并发处理。

## 核心特性与技术栈



### 1. 网络与 I/O 基础

- **I/O 模型:** 基于 Linux **Epoll** 的反应堆 (Reactor) 模式。
- **触发模式:** 采用 **边缘触发 (ET)** 模式，通过循环 I/O 操作确保处理完所有事件。
- **Socket 封装:** 实现了 RAII 机制，所有 Socket 均设置为**非阻塞**模式，并支持 **SO_REUSEADDR** 端口复用。
- **Buffer 机制:** 实现了高效的动态缓冲区 (`Buffer`)，通过 `readv` 系统调用支持**分散读取**，并能在需要时自动扩容。
- **零拷贝 (Zero-Copy):** 静态文件服务（GET请求）采用 `sendfile` 系统调用，减少内核态与用户态之间的数据拷贝，提升文件传输效率。

### 2. 并发架构 (Reactor + ThreadPool)

- **线程池 (ThreadPool):** 采用 C++ `std::thread`、`std::mutex` 和 `std::condition_variable` 实现固定大小的线程池，负责处理所有 CPU 密集型的 HTTP 解析与业务逻辑任务。
- **I/O 与逻辑解耦:** 主线程 (Reactor 线程) 专门负责 Epoll 监听和 `accept`, `read`, `write` 等 I/O 事件。
- **任务分发:** 主线程在读取到数据后 (`handleRead`)，将 HTTP 解析和处理任务 (`handleProcess`) 封装成 `std::function` 并提交到线程池。
- **线程通信:** 采用 `eventfd` 实现工作线程 (Worker) 到主线程 (I/O 线程) 的安全通信。当工作线程处理完业务并需要修改 FD 监听状态时（如添加 `EPOLLOUT`），它通过回调将任务推入一个带锁队列，并 `write` `eventfd` 来唤醒主线程的 Epoll 循环，主线程再安全地执行 `epoll_ctl` 操作。

### 3. HTTP 协议处理与服务

- **状态机解析:** 实现了精细的状态机 (`HttpConnection`)，能够完整解析**请求行**、**请求头部** 和 **Chunked Body** (包括 Footer)。
- **长连接支持 (Keep-Alive):** 能够正确解析和设置 `Connection` 头部，支持长连接模式。
- **连接管理:** 通过 `std::map` 管理连接，并实现基于 `std::chrono` 的**空闲连接超时清理**机制。
- **文件服务与写入:**
  - 实现了 **GET/HEAD 请求**的静态文件读取和响应逻辑。
  - 实现了 **PUT 请求**：支持目标文件的创建、替换和写入。
  - 实现了 **POST 请求**：支持协议体解析和接收确认 (204 No Content)。
- **安全性与健robustness:**
  - 包含**目录穿越安全检查** (`realpath`) 和文件类型检查 (MIME)。
  - 实现了**超大 Header 限制**和**连接超时**功能。
  - 支持 404 Not Found, 403 Forbidden, 405 Method Not Allowed 等多种错误状态。

## 项目结构



```
.
├── CMakeLists.txt          # CMake 构建脚本 (C++23)
├── README.md               # 项目说明文件
├── include
│   ├── Buffer.h            # 动态 I/O 缓冲区
│   ├── HttpConnection.h    # 单个 HTTP 连接处理类
│   ├── HttpRequest.h       # HTTP 请求数据结构
│   ├── HttpResponse.h      # HTTP 响应数据结构
│   ├── ThreadPool.h        # 线程池头文件
│   ├── epoll.h             # Epoll 封装
│   └── socket.h            # Socket 封装
└── src
    ├── Buffer.cpp
    ├── HttpConnection.cpp  # 核心状态机、MIME 映射、文件服务/写入逻辑
    ├── HttpResponse.cpp
    ├── ThreadPool.cpp      # 线程池实现
    ├── main.cpp            # 服务器主循环、线程池调度、eventfd 通信
    ├── epoll.cpp
    ├── socket.cpp
```



## 构建与运行



本项目使用 CMake 构建系统。

### 1. 环境要求

- C++ 编译器（支持 C++23 标准，如 GCC 13+ 或 Clang）
- CMake (版本 3.15+)
- Linux 环境 (依赖 Epoll, eventfd, sendfile)

### 2. 构建步骤

Bash

```bash
# 1. 创建并进入构建目录
mkdir build
cd build

# 2. 运行 CMake 配置项目 (确保在项目根目录运行 cmake ..)
cmake ..

# 3. 编译项目
make

# 4. 运行服务器 (生成的程序名为 MyProjectExec，默认监听 0.0.0.0:8080)
./MyProjectExec
```

### 3. 运行前准备

要在本地测试静态文件服务和文件写入功能，请在项目根目录创建 `webroot` 文件夹。

Bash

```bash
# 在项目根目录执行
mkdir webroot
echo "<h1>Hello World!</h1>" > webroot/index.html
```

## 开发计划与进度



本项目已完成对协议健壮性（阶段 I）和并发架构升级（阶段 II）的优化。

| **阶段** | **重点目标**               | **状态**   | **关键技术**                                                 |
| -------- | -------------------------- | ---------- | ------------------------------------------------------------ |
| **I**    | **协议健壮性与单线程稳定** | **已完成** | Epoll ET、长连接、Chunked Body Footer 处理、连接超时、基础 PUT/POST 业务逻辑。 |
| **II**   | **并发架构升级**           | **已完成** | **线程池**、**eventfd 线程通信**、I/O 与逻辑解耦。           |
| **III**  | **高级特性与优化**         | **计划中** | 异步线程安全日志系统、HTTP/1.1 Range 请求支持、HTTPS/TLS 支持。 |
