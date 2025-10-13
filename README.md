# httpccc: 高性能 C++ HTTP 服务器





## 概述



`httpccc` 是一个基于现代 C++23 标准和 Linux Epoll 技术的轻量级、高性能 HTTP 服务器项目。本项目旨在提供一个稳健的网络基础框架，采用 **Epoll 边缘触发 (ET) 模式**实现高效的 I/O 多路复用，并支持基础的 **静态文件服务**、**文件写入**和 **长连接**。

## 核心特性与技术栈



### 1. 网络与 I/O 基础

- **I/O 模型:** 基于 Linux **Epoll** 的反应堆 (Reactor) 模式。
- **触发模式:** 采用 **边缘触发 (ET)** 模式，通过循环 I/O 操作确保处理完所有事件。
- **Socket 封装:** 实现了 RAII 机制，所有 Socket 均设置为**非阻塞**模式，并支持 **SO_REUSEADDR** 端口复用。
- **Buffer 机制:** 实现了高效的动态缓冲区 (`Buffer`)，在缓冲区写入时采用了当前空间与备用栈空间，避免了较大数据写入时可能发生丢失的情况，通过 `readv` 系统调用支持**分散读取**。

### 2. HTTP 协议处理（解析与服务）



- **状态机解析:** 实现了精细的状态机，能够完整解析**请求行**、**请求头部**和支持 **Chunked Body Footer** 完整处理。
- **长连接支持 (Keep-Alive):** 能够正确解析和设置 `Connection` 头部，支持长连接模式。
- **文件服务与写入 (已完成):**
  - 实现了 **GET/HEAD 请求**的文件读取和响应逻辑。
  - 新增支持 **PUT 请求**：实现目标文件的创建、替换和写入逻辑。
  - 新增支持 **POST 请求**：实现协议体解析和接收确认 (204 No Content)。
- **安全性与健壮性:**
  - 包含**目录穿越安全检查**和**文件类型检查**，防止对目录进行写入和读取。
  - 加入了**超大header限制**和**超时限制**。
  - 处理 **404 Not Found**、**403 Forbidden**、**405 Method Not Allowed** 等多种错误状态。

## 项目结构



```
.
├── CMakeLists.txt          # CMake 构建脚本 (C++23)
├── README.md               # 项目说明文件
├── include
│   ├── Buffer.h            # 动态 I/O 缓冲区头文件
│   ├── HttpConnection.h    # 单个 HTTP 连接处理类头文件
│   ├── HttpRequest.h       # HTTP 请求数据结构
│   ├── HttpResponse.h      # HTTP 响应数据结构 (含 reset 机制)
│   ├── epoll.h             # Epoll 封装
│   └── socket.h            # Socket 封装
└── src
    ├── Buffer.cpp
    ├── HttpConnection.cpp  # 核心状态机、MIME 映射、文件服务/写入逻辑
    ├── HttpResponse.cpp    # 响应序列化和 reset 实现
    ├── main.cpp            # 服务器主循环和连接管理
    ├── epoll.cpp
    ├── socket.cpp
    └── ...
```



## 构建与运行



本项目使用 CMake 构建系统。

### 1. 环境要求

- C++ 编译器（支持 C++23 标准，如 GCC 13+ 或 Clang）
- CMake (版本 3.15+)

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

# 4. 返回根目录后运行服务器 (生成的程序名为 MyProjectExec，默认监听 0.0.0.0:8080)
./build/MyProjectExec
```

### 3. 运行前准备

要在本地测试静态文件服务和文件写入功能，请在项目根目录创建 `webroot` 文件夹，并在其中放置静态文件。

Bash

```bash
# 在项目根目录执行
mkdir webroot
echo "<h1>Hello World!</h1>" > webroot/index.html
```

## 开发计划与进度



本项目已完成对协议健壮性（阶段 I）的优化，即将进入并发架构升级阶段。

| 阶段    | 重点目标                   | 状态        | 关键技术                                                     |
| ------- | -------------------------- | ----------- | ------------------------------------------------------------ |
| **I**   | **协议健壮性与单线程稳定** | **已完成**  | Epoll ET、长连接、Chunked Body Footer 处理、连接超时、基础 PUT/POST 业务逻辑。 |
| **II**  | **并发架构升级**           | **进行中 ** | 线程池基础架构、连接回调与线程安全隔离、Epoll 主线程与工作线程解耦。 |
| **III** | **高级特性与优化**         | **计划中**  | 异步线程安全日志系统、HTTP/1.1 Range 请求支持、HTTPS/TLS 支持。 |

