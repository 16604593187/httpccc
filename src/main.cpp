#include "socket.h"
#include "epoll.h" // 引入 Epoll 类
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>      // for close()
#include <sys/epoll.h>   // for EPOLLIN, EPOLLET
#include <arpa/inet.h>   // for inet_ntoa (打印IP)
#include<cstring>
#include<cerrno>
// 定义常量
const std::string SERVER_IP = "0.0.0.0"; 
const uint16_t SERVER_PORT = 8080;
const int LISTEN_BACKLOG = 1024;
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
                        Socket::set_nonblocking(client_fd);//设置非阻塞状态
                        // --- 接受成功处理 ---
                        epoll_poller.add_fd(client_fd, EPOLLIN | EPOLLET);//将客户端注册到epoll，监听可读事件与边缘触发事件
                        // 打印客户端信息
                        std::cout << "--- New connection accepted ---" << std::endl;
                        std::cout << "  Client IP: " << inet_ntoa(client_addr.sin_addr) 
                                  << ", Port: " << ntohs(client_addr.sin_port) 
                                  << ", New FD: " << client_fd << std::endl;
                    } // end while(true) accept loop

                } // end if (listen_socket event)
                else if(event_type&EPOLLIN){
                    while(true){
                        char buffer[1024];
                        ssize_t bytes_read = ::read(current_fd, buffer, sizeof(buffer));
                        if(bytes_read==-1){
                            if(errno==EAGAIN||errno==EWOULDBLOCK)break;
                            else{
                                std::cerr << "Read error on FD " << current_fd << ": " << strerror(errno) << std::endl;
                                epoll_poller.del_fd(current_fd);
                                ::close(current_fd);
                                break;
                            }
                        }
                        else if(bytes_read==0){
                            std::cout << "Client closed connection on FD " << current_fd << std::endl;
                            epoll_poller.del_fd(current_fd);
                            ::close(current_fd);
                            break;
                        }
                        else{
                            epoll_poller.mod_fd(current_fd, EPOLLOUT | EPOLLET); 
                            std::cout << "Read " << bytes_read << " bytes from FD " << current_fd << ": [Data Received]" << std::endl;

                        }
                    }
                }
                else if(event_type&EPOLLOUT){
                    const char* message = "Echo successful: Data received and sent back.\n";
                    size_t len = strlen(message);
                    ssize_t bytes_written = 0;
                    while(true){
                        ssize_t written_now = ::write(current_fd, message + bytes_written, len - bytes_written);
                        if(written_now==-1){
                            if(errno==EAGAIN||errno==EWOULDBLOCK){
                                std::cout << "Write buffer full, waiting for next EPOLLOUT." << std::endl;
                                break;
                            }else{
                                std::cerr << "Write error on FD " << current_fd << ": " << strerror(errno) << std::endl;
                                epoll_poller.del_fd(current_fd);
                                ::close(current_fd);
                                break;
                            }
                        }
                        else{
                            bytes_written+=written_now;
                            if(bytes_written==len){
                                epoll_poller.mod_fd(current_fd, EPOLLIN | EPOLLET);
                                std::cout << "Data sent. FD " << current_fd << " switched to EPOLLIN." << std::endl;
                                break;
                            }
                        }
                    }
                }
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