#ifndef HTTPCCC_HTTPCONNECTION_H // 【新增：检查是否已定义】
#define HTTPCCC_HTTPCONNECTION_H // 【新增：如果未定义，则定义】
#include "Buffer.h"
#include<memory>
#include<sys/socket.h>
#include<unistd.h>
#include<cerrno>
#include<stdexcept>
#include "socket.h"
#include<cstring>
#include<iostream>
#include<string>
#include<functional>
#include "epoll.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
using EpollCallback = std::function<void(int fd, uint32_t events)>; //接受fd和新的epoll事件类型
class HttpConnection:public std::enable_shared_from_this<HttpConnection>{
private:
    int _clientFd;
    Buffer _inBuffer;//读缓冲区
    Buffer _outBuffer;//写缓冲区

    void closeConnection();
    EpollCallback _mod_callback;
    EpollCallback _close_callback;
    void updateEvents(uint32_t events);

    HttpRequest _httpRequest;
    HttpResponse _httpResponse;
    HttpRequestParseState _httpRPS;

    bool parseRequest();//负责解析并更新_httpRPS状态
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    void processRequest();//负责处理请求并生成响应
    bool shouldHaveBody()const;
    std::string trim(const std::string& str);
public:
    HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb);//接管fd并设置非阻塞
    ~HttpConnection();
    HttpConnection(const HttpConnection&)=delete;
    HttpConnection& operator=(const HttpConnection&)=delete;
    void handleRead();
    void handleWrite();
    void handleClose();
    int fd() const {return _clientFd;}
};
#endif