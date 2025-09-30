#include "epoll.h"
#include<stdexcept>
#include<cstring>
#include<cerrno>
#include<iostream>
#include<sys/epoll.h>
Epoll::Epoll(){
    if((_epollfd=epoll_create1(0))==-1){//初始化事件数组
        std::string error_msg = "Epoll creation failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
    _events.resize(MAX_EVENTS);//分配空间并检查
    if(_events.empty()&&MAX_EVENTS>0){
        throw std::runtime_error("Epoll event vector initialization failed.");
    }
}
Epoll::~Epoll(){
    if(_epollfd>=0){
        if(::close(_epollfd)==-1){
            std::string error_msg = "Epoll close failed:" + std::string(strerror(errno));
            std::cerr << error_msg << std::endl; 
        }
        _epollfd=-1;
    }
}
void Epoll::add_fd(int fd,uint32_t events){
    epoll_event event;//创建epoll_event结构体
    memset(&event,0,sizeof(event));//初始化event和data.fd
    event.events=events;
    event.data.fd=fd;
    if(::epoll_ctl(_epollfd, EPOLL_CTL_ADD, fd, &event)==-1){//将socket fd添加到epoll监控列表
        std::string error_msg = "Epoll add_fd failed:" + std::string(strerror(errno));
        throw std::runtime_error(error_msg);
    }
}
int Epoll::wait(int timeout_ms){
    int num_events;
    if(_epollfd<0)throw std::runtime_error("Epoll instance is not valid.");
    while(true){
        num_events=::epoll_wait(_epollfd, _events.data(), MAX_EVENTS, timeout_ms);
        if(num_events==-1){
            if(errno==EINTR)continue;//如果是中断事件直接continue
            else{
                std::string error_msg = "Epoll wait failed: " + std::string(strerror(errno));
                throw std::runtime_error(error_msg);
            }

        }
        break;
    }
    return num_events;
}
