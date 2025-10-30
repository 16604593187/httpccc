#include "socket.h"
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include<iostream>
Socket::Socket(){
    //创建成员
    if((_sockfd=socket(AF_INET,SOCK_STREAM,0))==-1){
        std::string error_msg = "Socket creation failed: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    Socket::set_nonblocking(_sockfd);
    int opt_val=1;
    if(setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val))==-1){//端口复用设置
        std::string error_msg = "SO_REUSEADDR setting failed: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
void Socket::bind(const std::string& ip,uint16_t port){ 
    memset(&_serv_addr,0,sizeof(_serv_addr));
    _serv_addr.sin_family=AF_INET;
    if((inet_pton(AF_INET,ip.c_str(),&_serv_addr.sin_addr.s_addr))<=0){
        if(errno==0)throw std::runtime_error("Bind failed: Invalid IP address format.");
        else{
            std::string error_msg = "Bind failed: inet_pton system error: " + std::string(strerror(errno));
            throw std::runtime_error(error_msg);
        }
    }
    _serv_addr.sin_port=htons(port);
    if(::bind(_sockfd,(struct sockaddr *)&_serv_addr,sizeof(_serv_addr))==-1){
        std::string error_msg = "Socket bind failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
void Socket::listen(int backlog){
    if((::listen(_sockfd,backlog))==-1){
        std::string error_msg = "Listen failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
Socket::~Socket(){
    if(_sockfd>=0){
        if(::close(_sockfd)==-1){
        std::string error_msg = "Close failed:" + std::string(strerror(errno));
        std::cerr << error_msg << std::endl; 
        }
        _sockfd=-1;
    }
}
int Socket::fd() const {
    return _sockfd;
}
int Socket::accept(struct sockaddr_in& client_addr){
    memset(&client_addr,0,sizeof(client_addr));
    socklen_t addr_len=sizeof(client_addr);
    int client_fd;
    if((client_fd=::accept(_sockfd, (struct sockaddr *)&client_addr, &addr_len))==-1){
        if(errno==EAGAIN||errno==EWOULDBLOCK){
            return -1;
        }else{
            std::string error_msg = "Accept failed:" + std::string(strerror(errno));
            throw std::runtime_error(error_msg);
        }
    }
    return client_fd;
}
void Socket::set_nonblocking(int fd){
    int vatc;
    if((vatc=::fcntl(fd,F_GETFL))==-1){
        std::string error_msg = "F_GETFL get failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    vatc|=O_NONBLOCK;
    if(::fcntl(fd,F_SETFL,vatc)==-1){
        std::string error_msg = "Nonblocking setting failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}