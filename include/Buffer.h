#ifndef HTTPCCC_BUFFER_H // 【新增：检查是否已定义】
#define HTTPCCC_BUFFER_H // 【新增：如果未定义，则定义】
#include<vector>
#include<string>
#include<unistd.h>
class Buffer{
private:
    std::vector<char>_buffer;
    size_t _readIndex;//已确认(读)部分索引
    size_t _writeIndex;//已发送(写)部分索引
    static const size_t kInitialSize=1024;//缓冲区初始大小
    void ensureWritableBytes(size_t len);//动态扩增缓冲区以确保缓冲区大小足够
    
public:
    Buffer(size_t initialSize=kInitialSize);
    size_t readableBytes() const;//获取有效数据的长度
    size_t writableBytes() const;//获取可写入数据的长度
    ssize_t readFd(int fd,int* savedErrno);//从Fd读取数据到Buffer
    void retrieve(size_t len);//向前推进数据指针
    //辅助函数
    char* begin() { return _buffer.data(); }
    const char* begin() const { return _buffer.data(); }
    size_t prependableBytes() const { return _readIndex; }
    void append(const char* data,size_t len);//调用ensureWritableBytes并且更新_writeIndex
    const char* findCRLF() const;//查找CRLF
    const char* findCRLF(const char* start) const;//查找指定范围内的CRLF
};
#endif