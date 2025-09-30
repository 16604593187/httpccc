#include "socket.h"
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include<iostream>
Socket::Socket(){//调用socket()系统调用，创建成员
    //创建成员
    if((_sockfd=socket(AF_INET,SOCK_STREAM,0))==-1){//错误审查，如果返回值为-1，则发出错误报告，返回异常
        std::string error_msg = "Socket creation failed: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    int vatc;//非阻塞状态设置
    if((vatc=::fcntl(_sockfd, F_GETFL))==-1){
        std::string error_msg = "标志获取失败: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    vatc|=O_NONBLOCK;
    if(fcntl(_sockfd, F_SETFL, vatc)==-1){
        std::string error_msg = "非阻塞状态设置失败: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    int opt_val=1;
    if(setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val))==-1){//端口复用设置
        std::string error_msg = "SO_REUSEADDR setting failed: " + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
void Socket::bind(const std::string& ip,uint16_t port){ 
    memset(&_serv_addr,0,sizeof(_serv_addr));//使用memset进行零初始化，所传入的三个参数分别是要初始化的内存地址，初始化成的值，初始化的字节数
    _serv_addr.sin_family=AF_INET;//设置IPV4地址簇
    if((inet_pton(AF_INET,ip.c_str(),&_serv_addr.sin_addr.s_addr))<=0){//设置ip地址
        if(errno==0)throw std::runtime_error("Bind failed: Invalid IP address format.");
        else{
            std::string error_msg = "Bind failed: inet_pton system error: " + std::string(strerror(errno));
            throw std::runtime_error(error_msg);
        }
    }
    _serv_addr.sin_port=htons(port);//设置端口号
    if(::bind(_sockfd,(struct sockaddr *)&_serv_addr,sizeof(_serv_addr))==-1){//调用系统bind()并做错误检查
        std::string error_msg = "Socket bind failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
void Socket::listen(int backlog){
    if((::listen(_sockfd,backlog))==-1){//调用系统调用listen进行监听
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
    // fd() 只是返回私有成员 _sockfd 的值
    return _sockfd;
}
int Socket::accept(struct sockaddr_in& client_addr){
    memset(&client_addr,0,sizeof(client_addr));//初始化地址结构，即其值与长度
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