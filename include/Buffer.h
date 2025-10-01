#ifndef HTTPCCC_BUFFER_H // 【新增：检查是否已定义】
#define HTTPCCC_BUFFER_H // 【新增：如果未定义，则定义】
#include<vector>
#include<string>
#include<unistd.h>
class Buffer{
private:
    std::vector<char>_buffer;
    size_t _readIndex;
    size_t _writeIndex;
    static const size_t kInitialSize=1024;
    void ensureWritableBytes(size_t len);
    
public:
    Buffer(size_t initialSize=kInitialSize);
    size_t readableBytes() const;//获取有效数据的长度
    size_t writableBytes() const;//获取可写入数据的长度
    ssize_t readFd(int fd,int* savedErrno);//从Fd读取数据到Buffer
    void retrieve(size_t len);//向前推进数据指针
    char* begin() { return _buffer.data(); }
    const char* begin() const { return _buffer.data(); }
    size_t prependableBytes() const { return _readIndex; }
    void append(const char* data,size_t len);
};
#endif