#include "Buffer.h"
#include<sys/uio.h>
#include<unistd.h>
#include<algorithm>
#include<cstring>
#include<cerrno>
Buffer::Buffer(size_t initialSize):_buffer(initialSize),_readIndex(0),_writeIndex(0){}
size_t Buffer::readableBytes() const{
    return _writeIndex-_readIndex;
}
size_t Buffer::writableBytes() const{
    return _buffer.size()-_writeIndex;
}
void Buffer::ensureWritableBytes(size_t len){
    if(writableBytes()>=len)return;
    if(writableBytes()+prependableBytes()>=len){
        char* read_ptr = begin() + _readIndex;
        char* write_ptr=begin()+_writeIndex;
        char* dest_ptr=begin();
        std::move(read_ptr,write_ptr,dest_ptr);
        size_t readable=_writeIndex-_readIndex;
        _readIndex=0;
        _writeIndex=readable;
    }
    else{
        size_t _capacity=_writeIndex+len;
        _buffer.resize(_capacity);
    }
}
void Buffer::append(const char* data,size_t len){
    ensureWritableBytes(len);
    char* target=begin()+_writeIndex;
    std::memcpy(target,data,len);
    _writeIndex+=len;
}
ssize_t Buffer::readFd(int fd,int* savedErrno){
    const size_t kExtraBufSize=65536;
    char extra_buf[kExtraBufSize];//定义栈上备用缓冲区
    struct iovec vec[2];//I/O向量结构体

    const size_t internal_writable=writableBytes();
    vec[0].iov_base=begin()+_writeIndex;//vec[0]可写入空间
    vec[0].iov_len=internal_writable;

    vec[1].iov_base=extra_buf;//vec[1]备用缓冲区
    vec[1].iov_len=kExtraBufSize;

    const int iovcnt =2;
    const ssize_t n=::readv(fd,vec,iovcnt);//执行readv系统调用

    //以下为错误检查部分
    if(n<0){
        *savedErrno=errno;
        return -1;
    }else if(n==0)return 0;
    else if(n<=internal_writable){
        _writeIndex+=n;
    }else{
        _writeIndex=_buffer.size();
        const size_t excess=n-internal_writable;
        append(extra_buf,excess);
    }
    return n;
}
void Buffer::retrieve(size_t len){
    if(len>readableBytes())len=readableBytes();
    _readIndex+=len;
    if(_readIndex==_writeIndex){
        _readIndex=0;
        _writeIndex=0;
    }
}