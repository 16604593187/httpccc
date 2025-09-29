#include "socket.h" // 你的 Socket 类
#include <iostream>
#include <stdexcept>
#include <string>

// 定义常量
const std::string SERVER_IP = "0.0.0.0"; // 监听所有接口
const uint16_t SERVER_PORT = 8080;
const int LISTEN_BACKLOG = 1024;
int main() {
    try {
        // 1. 实例化 Socket 对象 (执行创建、非阻塞、端口复用)
        Socket server_socket;
        std::cout << "Socket created successfully. FD: " << server_socket.fd() << std::endl;

        // 2. 绑定到地址和端口
        server_socket.bind(SERVER_IP, SERVER_PORT);
        std::cout << "Socket bound to " << SERVER_IP << ":" << SERVER_PORT << std::endl;

        // 3. 开始监听
        server_socket.listen(LISTEN_BACKLOG);
        std::cout << "Server is listening. Ready for connections..." << std::endl;

        // 4. 程序运行在这里，等待手动停止 (Ctrl+C)
        // 在正式集成 Epoll 之前，我们让程序在这里保持运行
        while (true) {
            // 简单地让程序保持运行，直到被用户终止
            // 真实服务器会在这里调用 epoll_wait()
            sleep(1); 
        }

    } catch (const std::runtime_error& e) {
        // 捕获你在 Socket.cpp 中抛出的所有异常
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1; // 返回非零值表示程序失败
    } catch (const std::exception& e) {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}