# httpccc: 高性能 C++ HTTP 服务器

## 概述

`httpccc` 是一个基于现代 C++23 标准和 Linux Epoll 技术的轻量级、高性能 HTTP 服务器项目。本项目旨在提供一个稳健的网络基础框架，采用 **Epoll 边缘触发 (ET) 模式**实现高效的 I/O 多路复用，并支持基本的 **静态文件服务** 和 **长连接**。

## 核心特性与技术栈

### 1. 网络与 I/O 基础

- **I/O 模型:** 基于 Linux **Epoll** 的反应堆 (Reactor) 模式。
- **触发模式:** 采用 **边缘触发 (ET)** 模式，通过循环 I/O 操作确保在一次通知中处理完所有事件，提高并发效率。
- **Socket 封装:** 实现了 RAII 机制，所有 Socket 均设置为**非阻塞**模式，并支持 **SO_REUSEADDR** 端口复用。
- **Buffer 机制:** 实现了高效的动态缓冲区 (`Buffer`)，通过 `readv` 系统调用支持**分散读取**，并优化了内存使用。

### 2. HTTP 协议处理（解析与服务）

- **状态机解析:** 在 `HttpConnection` 中实现了精细的状态机，能够完整解析**请求行**和所有**请求头部**。
- **长连接支持 (Keep-Alive):** 能够正确解析和设置 `Connection` 头部，支持长连接模式，并通过 `HttpResponse::reset()` 机制确保连接复用时的状态清理。
- **静态文件服务（已完成）:**
  - 实现了 **GET 请求**的文件读取和响应逻辑。
  - 内置 **MIME 类型映射**，根据文件扩展名动态设置 `Content-Type`。
  - 处理 **404 Not Found**、**403 Forbidden** 等文件相关的错误，并包含**目录穿越安全检查**。
- **消息体解析 (Content-Length):** 实现了基于 `Content-Length` 的消息体读取逻辑，并能正确处理数据不足时的非阻塞等待状态。
- **错误处理:** 对不支持的 Method 返回 **405 Method Not Allowed**。

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
    ├── HttpConnection.cpp  # 核心状态机、MIME 映射、文件服务逻辑
    ├── HttpResponse.cpp    # 响应序列化和 reset 实现
    ├── main.cpp            # 服务器主循环和连接管理
    └── ...
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

# 2. 运行 CMake 配置项目 (确保在项目根目录运行 cmake ..)
cmake ..

# 3. 编译项目
make

# 4. 返回根目录后运行服务器 (生成的程序名为 MyProjectExec，默认监听 0.0.0.0:8080)
./build/MyProjectExec
```

### 3. 运行前准备

要在本地测试静态文件服务，请在项目根目录创建 `webroot` 文件夹，并在其中放置 `index.html` 或其他静态文件。

Bash

```
# 在项目根目录执行
mkdir webroot
echo "<h1>Hello World!</h1>" > webroot/index.html
```
