#include "socket.h"
#include "epoll.h" // 引入 Epoll 类
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>      // for close()
#include <sys/epoll.h>   // for EPOLLIN, EPOLLET
#include <arpa/inet.h>   // for inet_ntoa (打印IP)

// 定义常量
const std::string SERVER_IP = "0.0.0.0"; 
const uint16_t SERVER_PORT = 8080;
const int LISTEN_BACKLOG = 1024;

// 注意：main 函数中包含了新的头文件引用，请确保它们都在你的环境中可用

int main() {
    // 客户端地址结构体，用于接收 accept 结果
    struct sockaddr_in client_addr; 
    
    try {
        // --- 1. 服务器初始化 ---
        
        // 创建并配置监听 Socket (自动完成非阻塞和端口复用)
        Socket listen_socket; 
        listen_socket.bind(SERVER_IP, SERVER_PORT);
        listen_socket.listen(LISTEN_BACKLOG);
        
        std::cout << "Server initialized and listening on " << SERVER_IP << ":" 
                  << SERVER_PORT << ". FD: " << listen_socket.fd() << std::endl;

        // 创建 Epoll 实例
        Epoll epoll_poller; 
        
        // --- 2. 注册监听 Socket ---
        
        // 注册监听 Socket 到 Epoll，监听输入事件 (EPOLLIN) 并设置为边缘触发 (EPOLLET)
        epoll_poller.add_fd(listen_socket.fd(), EPOLLIN | EPOLLET); 
        std::cout << "Listening socket registered to Epoll." << std::endl;

        // --- 3. 事件主循环 ---
        
        while (true) {
            // 阻塞等待事件，返回就绪事件的数量
            int num_events = epoll_poller.wait(-1); 
            
            // 获取就绪事件数组
            const auto& events = epoll_poller.get_events(); 

            for (int i = 0; i < num_events; ++i) {
                int current_fd = events[i].data.fd; 
                uint32_t event_type = events[i].events;

                // 检查是否是监听 Socket 上的 EPOLLIN 事件 (新连接)
                if (current_fd == listen_socket.fd() && (event_type & EPOLLIN)) {
                    
                    // 边缘触发模式处理：循环调用 accept()
                    while (true) {
                        // 调用封装好的 Socket::accept()
                        // 客户端地址结构体在每次 accept 时都会被填充
                        int client_fd = listen_socket.accept(client_addr);
                        
                        if (client_fd == -1) {
                            // accept 返回 -1 (EAGAIN/EWOULDBLOCK)，表示所有排队的连接已处理完
                            break; 
                        }
                        
                        // --- 接受成功处理 ---
                        
                        // 打印客户端信息
                        std::cout << "--- New connection accepted ---" << std::endl;
                        std::cout << "  Client IP: " << inet_ntoa(client_addr.sin_addr) 
                                  << ", Port: " << ntohs(client_addr.sin_port) 
                                  << ", New FD: " << client_fd << std::endl;
                        
                        // IMPORTANT: 新的客户端 FD 也必须设置为非阻塞
                        // 假设我们现在直接注册客户端 FD (简单版本)
                        // TODO: 在这里调用 fcntl(client_fd, F_SETFL, O_NONBLOCK) 
                        // TODO: 注册 client_fd 到 Epoll：epoll_poller.add_fd(client_fd, EPOLLIN | EPOLLET);
                        
                        // 为了简化，我们暂时关闭连接，防止 FD 泄露，直到你实现 I/O 处理
                        ::close(client_fd);
                        std::cout << "--- Client FD " << client_fd << " closed temporarily. ---" << std::endl;

                    } // end while(true) accept loop

                } // end if (listen_socket event)
                
                // TODO: 这里的 else if 块将用于处理客户端的 I/O 事件
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << "Fatal Runtime Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "An unexpected C++ Error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0; 
}