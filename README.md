# httpccc: 高性能 C++ HTTP 服务器

## 概述

`httpccc` 是一个基于现代 C++23 标准和 Linux Epoll 技术的轻量级、高性能 HTTP 服务器项目。本项目旨在提供一个稳健、快速的网络基础框架，采用 **Epoll 边缘触发 (ET) 模式**实现高效的 I/O 多路复用，并结合精细的 HTTP 协议状态机解析。

## 核心特性与技术栈

### 1. 网络 I/O 框架

- **I/O 模型:** 基于 Linux **Epoll** 的反应堆 (Reactor) 模式。
- **触发模式:** 采用 **边缘触发 (ET)** 模式，通过在主循环中对监听 Socket 和客户端 Socket 进行循环 I/O 操作，最大限度地处理排队事件，提高并发性能。
- **Socket 封装:** * 实现了 RAII 机制的 `Socket` 类，在析构时自动关闭文件描述符。
  - 所有文件描述符（包括监听和客户端 FD）均设置为**非阻塞**模式。
  - 在 Socket 创建时自动设置了 **SO_REUSEADDR** 端口复用选项。
- **Epoll 封装:** 实现了 `Epoll` 类，封装了 `epoll_create1`、`epoll_ctl` (ADD/MOD/DEL) 等操作，并能在 `wait` 函数中正确处理 `EINTR` 中断事件。
- **连接管理:** 客户端连接由 `std::shared_ptr<HttpConnection>` 在 `main.cpp` 的 `std::map` 中管理，确保了资源的自动清理和线程安全生命周期控制。

### 2. I/O 和缓冲区管理

- **动态缓冲区 (Buffer):** 实现了高效的 `Buffer` 类，支持动态扩容，通过判断可写空间和已读空间优化内存使用，减少内存拷贝。
- **分散读取 (Scatter Read):** 读操作 `readFd` 使用 `readv` 系统调用，利用栈上备用缓冲区来处理大块数据，有效避免缓冲区溢出，并提高 I/O 效率。

### 3. HTTP 协议处理

- **状态机解析:** `HttpConnection` 类内嵌了一个状态机 (`HttpRequestParseState`)，能够逐步解析 HTTP 请求的各个组成部分。
  - **请求行解析** (`kExpectRequestLine`)：识别 Method, URI, Version。
  - **头部解析** (`kExpectHeaders`)：通过 `\r\n\r\n` 识别头部结束，并支持头部字段名称的**大小写不敏感处理**。
  - **消息体准备** (`kExpectBody`)：已实现逻辑判断请求是否携带消息体 (Body)，并为 `Content-Length` 方式的消息体解析预留了状态和开始实现逻辑。
- **响应生成:** `HttpResponse` 类负责组装和序列化 HTTP 响应（状态行、Header 和 Body）到 `_outBuffer` 中。
- **长连接支持 (Keep-Alive):** 能够正确解析请求中的 `Connection` 头部，并设置响应，支持 HTTP 长连接或短连接模式。
- **基本业务逻辑:** 已实现针对 **GET** 请求的简单文本响应 (200 OK) 和对其他未支持 Method 的 **405 Method Not Allowed** 响应。

## 项目结构

```
.
├── CMakeLists.txt        # CMake 构建脚本，使用 C++23 标准
├── README.md             # 项目说明文件
├── include
│   ├── Buffer.h          # 动态 I/O 缓冲区声明
│   ├── epoll.h           # Epoll 封装类头文件
│   ├── HttpConnection.h  # 单个 HTTP 连接处理类头文件
│   ├── HttpRequest.h     # HTTP 请求数据结构头文件
│   ├── HttpResponse.h    # HTTP 响应数据结构头文件
│   └── socket.h          # Socket 封装头文件
└── src
    ├── Buffer.cpp        # 动态缓冲区实现
    ├── epoll.cpp         # Epoll 类实现
    ├── HttpConnection.cpp# 核心状态机和业务逻辑实现
    ├── HttpResponse.cpp  # HTTP 响应序列化实现
    ├── main.cpp          # 服务器主循环、Epoll 事件分发和连接管理
    └── socket.cpp        # Socket 类实现 (非阻塞、端口复用)
```



## 构建与运行

本项目使用 CMake 构建系统。

### 1. 环境要求

- C++ 编译器（支持 C++23 标准，如 GCC 13+ 或 Clang）
- CMake (版本 3.15+)

### 2. 构建步骤

Bash

```
# 1. 创建并进入构建目录
mkdir build
cd build

# 2. 运行 CMake 配置项目 (在项目根目录运行)
cmake ..

# 3. 编译项目
make

# 4. 运行服务器 (默认监听 0.0.0.0:8080)
./MyProjectExec
```
