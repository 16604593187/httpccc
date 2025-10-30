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
    Buffer _inBuffer;
    Buffer _outBuffer;
    
    EpollCallback _mod_callback;
    EpollCallback _close_callback;

    HttpRequest _httpRequest;
    HttpResponse _httpResponse;
    HttpRequestParseState _httpRPS;

    int _fileFd;
    size_t _fileTotalSize;
    off_t _fileSentOffset;
    //管理分块解析进度
    enum ChunkParseState{
        kExpectChunkSize,
        kExpectChunkData,
        kExpectChunkCRLF,
        kExpectChunkBodyDone
    };
    //超时处理
    using Clock=std::chrono::high_resolution_clock;
    Clock::time_point _lastActiveTime;
private:
    void updateEvents(uint32_t events);

    bool parseRequest();
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    void processRequest();
    void handleGetRequest();
    std::string getMimeType(const std::string& path);
    bool shouldHaveBody()const;
    std::string trim(const std::string& str);
    //chunked的消息体解析
    bool parseChunkedBody();
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
    void handleProcess();
    int fd() const {return _clientFd;}
    //超时处理
    void updateActiveTime(){_lastActiveTime=Clock::now();}
    Clock::time_point getLastActiveTime()const{return _lastActiveTime;}
    //新增辅助函数
    size_t inputReadableBytes() const { 
        return _inBuffer.readableBytes(); 
    }
};
#endif