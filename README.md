### 当前已实现功能


1. #### 基础框架与工具

  项目初始化：设置了 C++23 标准的 CMake 构建系统，并创建了 .gitignore 文件。

错误处理：在 Socket 和 Epoll 类中使用了 C++ 异常 (std::runtime_error) 来报告系统调用错误，提升了代码的健壮性。

2. #### Socket 封装 (Socket 类)

  RAII 实现：使用 Socket 类封装了文件描述符 (_sockfd)，并在析构函数中自动调用 close()，实现了资源获取即初始化 (RAII)。

非阻塞设置：在构造函数中自动将 Socket 设置为非阻塞模式 (O_NONBLOCK)。

端口复用：在构造函数中自动设置了 SO_REUSEADDR 选项。

非阻塞 Accept：实现了 accept 方法，并正确处理了非阻塞模式下的 EAGAIN/EWOULDBLOCK 错误。

3. #### Epoll 封装 (Epoll 类)

  Epoll 实例管理：使用 Epoll 类封装了 epoll_create1() 调用，并在析构函数中关闭 Epoll 文件描述符。

事件操作：实现了 add_fd、mod_fd 和 del_fd，用于管理 Epoll 监控列表。

事件等待：实现了 wait 方法，正确处理了 EINTR 中断事件。

4. #### 主循环逻辑 (main.cpp)

  ET 模式 Accept：在处理监听 Socket 上的 EPOLLIN 事件时，使用了 while(true) 循环调用 accept 来处理所有排队的连接，这是 Epoll ET 模式下的标准做法。

ET 模式 Read：在处理客户端 Socket 的 EPOLLIN 事件时，使用了 while(true) 循环调用 read 来读取缓冲区中的所有数据，处理了 EAGAIN/EWOULDBLOCK 和连接关闭 (bytes_read == 0) 的情况。

状态切换：实现了读写事件的状态切换：读取数据后将 FD 切换到 EPOLLOUT，发送数据完成后再切换回 EPOLLIN。