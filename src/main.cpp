#include "socket.h"
#include "epoll.h" 
#include "HttpConnection.h" // 【新增】引入 HttpConnection 类
#include "ThreadPool.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>      
#include <sys/epoll.h>   
#include <arpa/inet.h>   
#include <cstring>
#include <cerrno>
#include <map>           // 【新增】用于连接管理器
#include <memory>        // 【新增】用于智能指针
#include<chrono>
// 定义常量 (不变)
const std::string SERVER_IP = "0.0.0.0"; 
const uint16_t SERVER_PORT = 8080;
const int LISTEN_BACKLOG = 1024;
const int IDLE_TIMEOUT_SECONDS=60;
const int EPOLL_WAIT_TIMEOUT_MS=1000;
int main() {
    // --- 变量定义提升：确保在整个 main 作用域内可见 ---
    Socket listen_socket; 
    Epoll epoll_poller; 
    std::map<int, std::shared_ptr<HttpConnection>> connections; 

    // 【新增】统一的连接关闭和移除逻辑
    auto epoll_mod_cb = [&](int fd, uint32_t events) {
        epoll_poller.mod_fd(fd, events);
    };
    // 现在 Lambda 可以通过引用 [&] 安全地捕获 epoll_poller 和 connections
    auto close_and_remove_conn = [&](int fd,uint32_t events) {
        try {
        epoll_poller.del_fd(fd);
        } catch (const std::runtime_error& e) {
            std::cerr << "Warning: Epoll del_fd failed for FD " << fd << ": " << e.what() << std::endl;
        }

        // 从 Map 中移除。shared_ptr 自动调用 HttpConnection 析构函数，析构函数调用 closeConnection()，最终关闭 FD。
        connections.erase(fd);
        std::cout << "--- Connection closed and removed for FD: " << fd << " ---" << std::endl;
    };


    try {
        // --- 1. 服务器初始化 ---
        // 使用前面定义的 listen_socket 进行初始化
        listen_socket.bind(SERVER_IP, SERVER_PORT);
        listen_socket.listen(LISTEN_BACKLOG);
        
        std::cout << "Server initialized and listening on " << SERVER_IP << ":" 
                  << SERVER_PORT << ". FD: " << listen_socket.fd() << std::endl;

        
        // --- 2. 注册监听 Socket ---
        // 注册监听 Socket 到 Epoll，监听输入事件 (EPOLLIN) 并设置为边缘触发 (EPOLLET)
        epoll_poller.add_fd(listen_socket.fd(), EPOLLIN | EPOLLET); 
        std::cout << "Listening socket registered to Epoll." << std::endl;

        // --- 3. 事件主循环 ---
        
        while (true) {
            int num_events = epoll_poller.wait(EPOLL_WAIT_TIMEOUT_MS); 
            const auto& events = epoll_poller.get_events(); 
            // 1. 获取当前时间点
            using Clock = std::chrono::high_resolution_clock;
            auto currentTime = Clock::now();

            // 2. 遍历连接Map并清理
            // 2. 遍历连接Map并清理
            for (auto it = connections.begin(); it != connections.end(); /* ... */) {
                
                std::shared_ptr<HttpConnection> conn = it->second;
                // 3. 计算空闲时长
                auto lastActiveTime = conn->getLastActiveTime();
                auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastActiveTime);
                if (idleDuration.count() >= IDLE_TIMEOUT_SECONDS) {
                    int client_fd = it->first;
                    std::cout << "--- Connection timed out (FD: " << client_fd << "). Cleaning up. ---" << std::endl;

                    // 1. 【安全】手动从 Epoll 中移除 (防止定时器清理失败)
                    conn->closeConnection();
                    try {
                        epoll_poller.del_fd(client_fd);
                    } catch (const std::runtime_error& e) {
                        std::cerr << "Warning: Epoll del_fd failed for FD " << client_fd << ": " << e.what() << std::endl;
                    }

                    // 2. 【安全】调用内部资源清理 (关闭FD，不触发回调)
                    
                    
                    // 3. 【安全】从 Map 中删除并更新迭代器
                    it = connections.erase(it); 
                    std::cout << "--- Connection closed and removed by timer. ---" << std::endl;
                } else {
                    // 如果没有删除，则安全地移动到下一个元素
                    ++it; 
                }
            }
            for (int i = 0; i < num_events; ++i) {
                int current_fd = events[i].data.fd; 
                uint32_t event_type = events[i].events;

                // 检查是否是监听 Socket 上的 EPOLLIN 事件 (新连接)
                if (current_fd == listen_socket.fd() && (event_type & EPOLLIN)) {
                    
                    // 边缘触发模式处理：循环调用 accept()
                    while (true) {
                        struct sockaddr_in client_addr; // 必须在这里声明
                        int client_fd = listen_socket.accept(client_addr);
                        
                        if (client_fd == -1) {
                            break; // accept 返回 -1 (EAGAIN/EWOULDBLOCK)，表示连接已处理完
                        }
                        
                        // 【修改】新连接处理：创建 HttpConnection 对象并注册
                        try {
                            
                            // 1. 创建 HttpConnection 对象 (构造函数中已设置非阻塞)
                            std::shared_ptr<HttpConnection> conn = std::make_shared<HttpConnection>(
                                client_fd,
                                epoll_mod_cb,
                                close_and_remove_conn);
                            
                            // 2. 将 FD 注册到连接管理器中
                            connections[client_fd] = conn;
                            
                            // 3. 将 FD 注册到 Epoll，初始只监听读事件 (EPOLLIN | EPOLLET)
                            epoll_poller.add_fd(client_fd, EPOLLIN | EPOLLET);
                            
                            std::cout << "--- New connection accepted ---" << std::endl;
                            std::cout << "  Client IP: " << inet_ntoa(client_addr.sin_addr) 
                                      << ", Port: " << ntohs(client_addr.sin_port) 
                                      << ", New FD: " << client_fd << std::endl;
                        } catch (const std::exception& e) {
                             std::cerr << "Failed to establish new connection for FD " << client_fd << ": " << e.what() << std::endl;
                             if (client_fd >= 0) ::close(client_fd); // 确保关闭FD
                        }

                    } // end while(true) accept loop

                } // end if (listen_socket event)
                else { 
                    // 【修改】处理客户端 I/O 事件
                    auto it = connections.find(current_fd);
                    if (it == connections.end()) {
                        // 理论上不应发生，但如果Epoll报告了Map中没有的FD，进行清理
                        std::cerr << "Warning: Event reported for unknown FD " << current_fd << ". Attempting Epoll cleanup." << std::endl;
                        close_and_remove_conn(current_fd,0); // 清理Epoll和Map
                        continue;
                    }
                    
                    std::shared_ptr<HttpConnection> conn = it->second;

                    // 1. 事件分发
                    if (event_type & EPOLLIN) {
                        conn->handleRead();
                    }
                    if (event_type & EPOLLOUT) {
                        conn->handleWrite();
                    }
                    
                    // 2. 清理已关闭的连接
                    // HttpConnection::handleClose() 在关闭 FD 后，将 _clientFd 设为 -1
                    if (conn->fd() < 0) { 
                        try {
                            epoll_poller.del_fd(current_fd);
                        } catch (const std::runtime_error& e) {
                            std::cerr << "Warning: Epoll del_fd failed for FD " << current_fd << ": " << e.what() << std::endl;
                        }

                    // 2. 从 Map 中移除
                        connections.erase(current_fd);
                        std::cout << "--- Connection closed and removed for FD: " << current_fd << " ---" << std::endl;
                        continue; // 立即跳到下一个事件
                        // close_and_remove_conn 会负责调用 epoll_poller.del_fd
                        //close_and_remove_conn(current_fd,0); 
                    } else {
                        // 【下一步需完善：事件切换】
                        // 现在我们需要让 HttpConnection 能够调用 epoll_poller.mod_fd
                        // 这将是我们下一步实现协议解析的关键控制逻辑。
                    }
                } // end else (client event dispatch)
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