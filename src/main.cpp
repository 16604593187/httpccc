#include "socket.h"
#include "epoll.h" 
#include "HttpConnection.h"
#include "ThreadPool.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>      
#include <sys/epoll.h>   
#include<sys/eventfd.h>
#include<mutex>
#include<queue>
#include<unistd.h>
#include <arpa/inet.h>   
#include <cstring>
#include <cerrno>
#include <map> //用于连接管理器
#include <memory>//用于智能指针
#include<chrono>
const std::string SERVER_IP = "0.0.0.0"; 
const uint16_t SERVER_PORT = 8080;
const int LISTEN_BACKLOG = 1024;
const int IDLE_TIMEOUT_SECONDS=60;
const int EPOLL_WAIT_TIMEOUT_MS=1000;
struct EpollTask{
    int fd;
    uint32_t events;
};
std::mutex g_task_mutex;
std::queue<EpollTask>g_task_queue;
int g_event_fd=-1;
int main() {
    Socket listen_socket; 
    Epoll epoll_poller; 
    std::map<int, std::shared_ptr<HttpConnection>> connections; 
    g_event_fd=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    if(g_event_fd<0){
        throw std::runtime_error("eventfd creation failed: " + std::string(strerror(errno)));
    }
    auto epoll_mod_cb=[&](int fd,uint32_t events){
        {
            std::lock_guard<std::mutex>lock(g_task_mutex);
            g_task_queue.push({fd,events});
        }
        uint64_t one=1;
        ssize_t n= write(g_event_fd,&one,sizeof(one));
        if(n!=sizeof(one)){
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
             std::cerr << "Warning: Failed to write to eventfd: " << strerror(errno) << std::endl;
            }
        }
    };
    //线程池定义
    size_t num_cpus = std::thread::hardware_concurrency();
    size_t pool_size = num_cpus > 0 ? num_cpus : 4;
    ThreadPool thread_pool(pool_size);
    
    auto close_and_remove_conn = [&](int fd,uint32_t events) {
        try {
        epoll_poller.del_fd(fd);
        } catch (const std::runtime_error& e) {
            std::cerr << "Warning: Epoll del_fd failed for FD " << fd << ": " << e.what() << std::endl;
        }

        connections.erase(fd);
        std::cout << "--- Connection closed and removed for FD: " << fd << " ---" << std::endl;
    };


    try {
        //服务器初始化
        listen_socket.bind(SERVER_IP, SERVER_PORT);
        listen_socket.listen(LISTEN_BACKLOG);
        
        std::cout << "Server initialized and listening on " << SERVER_IP << ":" 
                  << SERVER_PORT << ". FD: " << listen_socket.fd() << std::endl;

        
        //注册监听 Socket
        epoll_poller.add_fd(listen_socket.fd(), EPOLLIN | EPOLLET); 
        std::cout << "Listening socket registered to Epoll." << std::endl;
        epoll_poller.add_fd(g_event_fd, EPOLLIN | EPOLLET); // 监听输入事件
        std::cout << "eventfd registered to Epoll for inter-thread signaling." << std::endl;
        //事件主循环
        while (true) {
            int num_events = epoll_poller.wait(EPOLL_WAIT_TIMEOUT_MS); 
            const auto& events = epoll_poller.get_events(); 
            //获取当前时间点
            using Clock = std::chrono::high_resolution_clock;
            auto currentTime = Clock::now();

            for (auto it = connections.begin(); it != connections.end(); /* ... */) {
                
                std::shared_ptr<HttpConnection> conn = it->second;
                //计算空闲时长
                auto lastActiveTime = conn->getLastActiveTime();
                auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastActiveTime);
                if (idleDuration.count() >= IDLE_TIMEOUT_SECONDS) {
                    int client_fd = it->first;
                    std::cout << "--- Connection timed out (FD: " << client_fd << "). Cleaning up. ---" << std::endl;

                    conn->closeConnection();
                    try {
                        epoll_poller.del_fd(client_fd);
                    } catch (const std::runtime_error& e) {
                        std::cerr << "Warning: Epoll del_fd failed for FD " << client_fd << ": " << e.what() << std::endl;
                    }

                    it = connections.erase(it); 
                    std::cout << "--- Connection closed and removed by timer. ---" << std::endl;
                } else {
                    ++it; 
                }
            }
            for (int i = 0; i < num_events; ++i) {
                int current_fd = events[i].data.fd; 
                uint32_t event_type = events[i].events;
                if(current_fd==g_event_fd&&(event_type&EPOLLIN)){
                    uint64_t value;
                    ssize_t n=read(g_event_fd,&value,sizeof(value));
                    if(n<0&&(errno!=EAGAIN&&errno!=EWOULDBLOCK)){
                        std::cerr << "Error reading eventfd: " << strerror(errno) << std::endl;
                        continue;
                    }
                    if(n<=0)continue;
                    std::queue<EpollTask>local_queue;
                    {
                        std::lock_guard<std::mutex>lock(g_task_mutex);
                        std::swap(local_queue,g_task_queue);
                    }
                    while(!local_queue.empty()){
                        EpollTask task=local_queue.front();
                        local_queue.pop();
                        if(connections.count(task.fd)>0){
                            epoll_poller.mod_fd(task.fd,task.events);
                        }
                        
                    }
                    continue;
                }
                if (current_fd == listen_socket.fd() && (event_type & EPOLLIN)) {
                    
                    while (true) {
                        struct sockaddr_in client_addr; 
                        int client_fd = listen_socket.accept(client_addr);
                        
                        if (client_fd == -1) {
                            break; 
                        }
                        
                        try {
                            
                            std::shared_ptr<HttpConnection> conn = std::make_shared<HttpConnection>(
                                client_fd,
                                epoll_mod_cb,
                                close_and_remove_conn);
                            
                            connections[client_fd] = conn;
                            
                            epoll_poller.add_fd(client_fd, EPOLLIN | EPOLLET);
                            
                            std::cout << "--- New connection accepted ---" << std::endl;
                            std::cout << "  Client IP: " << inet_ntoa(client_addr.sin_addr) 
                                      << ", Port: " << ntohs(client_addr.sin_port) 
                                      << ", New FD: " << client_fd << std::endl;
                        } catch (const std::exception& e) {
                             std::cerr << "Failed to establish new connection for FD " << client_fd << ": " << e.what() << std::endl;
                             if (client_fd >= 0) ::close(client_fd); 
                        }

                    } 

                } 
                else { 
                    auto it = connections.find(current_fd);
                    if (it == connections.end()) {
                        std::cerr << "Warning: Event reported for unknown FD " << current_fd << ". Attempting Epoll cleanup." << std::endl;
                        close_and_remove_conn(current_fd,0); 
                        continue;
                    }
                    
                    std::shared_ptr<HttpConnection> conn = it->second;

                    
                    if (event_type & EPOLLIN) {
                        conn->handleRead();
                        if (conn->fd() >= 0 && conn->inputReadableBytes() > 0) {
                            thread_pool.enqueue([conn](){ 
                                conn->handleProcess(); 
                            });
                        }
                    }
                    if (event_type & EPOLLOUT) {
                        conn->handleWrite();
                    }
                    
                    if (conn->fd() < 0) { 
                        try {
                            epoll_poller.del_fd(current_fd);
                        } catch (const std::runtime_error& e) {
                            std::cerr << "Warning: Epoll del_fd failed for FD " << current_fd << ": " << e.what() << std::endl;
                        }

                        connections.erase(current_fd);
                        std::cout << "--- Connection closed and removed for FD: " << current_fd << " ---" << std::endl;
                        continue; 
                    } else {
                    }
                } 
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << "Fatal Runtime Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "An unexpected C++ Error occurred: " << e.what() << std::endl;
        return 1;
    }
    if(g_event_fd>=0){
        ::close(g_event_fd);
    }
    return 0; 
}