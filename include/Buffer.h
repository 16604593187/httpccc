#ifndef HTTPCCC_BUFFER_H 
#define HTTPCCC_BUFFER_H 
#include<vector>
#include<string>
#include<unistd.h>
class Buffer{

public:
    Buffer(size_t initialSize=kInitialSize);
    size_t readableBytes() const;
    size_t writableBytes() const;
    ssize_t readFd(int fd,int* savedErrno);
    void retrieve(size_t len);
    //辅助函数
    char* begin() { return _buffer.data(); }
    const char* begin() const { return _buffer.data(); }
    size_t prependableBytes() const { return _readIndex; }
    void append(const char* data,size_t len);
    const char* findCRLF() const;
    const char* findCRLF(const char* start) const;
private:
    void ensureWritableBytes(size_t len);//动态扩增缓冲区
private:
    std::vector<char>_buffer;
    size_t _readIndex;
    size_t _writeIndex;
    static const size_t kInitialSize=1024;
};
#endif