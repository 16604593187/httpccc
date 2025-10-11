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
#include<limits.h>
#include<sys/stat.h>
#include<chrono>
using EpollCallback = std::function<void(int fd, uint32_t events)>; //接受fd和新的epoll事件类型
class HttpConnection:public std::enable_shared_from_this<HttpConnection>{
private:
    friend class HttpRequest;
    int _clientFd;
    Buffer _inBuffer;//读缓冲区
    Buffer _outBuffer;//写缓冲区

    
    EpollCallback _mod_callback;
    EpollCallback _close_callback;
    void updateEvents(uint32_t events);

    HttpRequest _httpRequest;
    HttpResponse _httpResponse;
    HttpRequestParseState _httpRPS;
    //以下三个成员为sendfile函数做准备
    int _fileFd;//待发送的文件描述符
    size_t _fileTotalSize;//待发送的文件大小
    off_t _fileSentOffset;//已发送的字节偏移量

    bool parseRequest();//负责解析并更新_httpRPS状态
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    void processRequest();//负责处理请求并生成响应
    void handleGetRequest(); //处理GET请求的文件服务逻辑
    std::string getMimeType(const std::string& path);//根据路径获取MIME类型
    bool shouldHaveBody()const;
    std::string trim(const std::string& str);

    //精确管理分块解析进度
    enum ChunkParseState{
        kExpectChunkSize,//期待读取下一个块的大小
        kExpectChunkData,//期待读取块本身
        kExpectChunkCRLF,//期待读取块数据结尾的\r\n
        kExpectChunkBodyDone//解析完成
    };
    
    //chunked的消息体解析
    bool parseChunkedBody();

    //超时处理
    using Clock=std::chrono::high_resolution_clock;
    Clock::time_point _lastActiveTime;
    //post和put请求支持
    void handlePostOrPutRequest();
public:
    void closeConnection();
    HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb);//接管fd并设置非阻塞
    ~HttpConnection();
    HttpConnection(const HttpConnection&)=delete;
    HttpConnection& operator=(const HttpConnection&)=delete;
    void handleRead();
    void handleWrite();
    void handleClose();
    int fd() const {return _clientFd;}

    //超时处理
    void updateActiveTime(){_lastActiveTime=Clock::now();}
    Clock::time_point getLastActiveTime()const{return _lastActiveTime;}
};
#endif