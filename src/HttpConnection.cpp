#include "HttpConnection.h"

HttpConnection::HttpConnection(int fd,EpollCallback mod_cb,EpollCallback close_cb):_clientFd(fd),_mod_callback(std::move(mod_cb)),_close_callback(std::move(close_cb)){
    if(_clientFd<0){
        throw std::runtime_error("Invalid file descriptor passed to HttpConnection.");
    }
    Socket::set_nonblocking(fd);

}
HttpConnection::~HttpConnection(){
    closeConnection();
}
void HttpConnection::closeConnection(){
    if(_clientFd>=0){
        if(::close(_clientFd)==-1){
            std::cerr << "Error closing FD " << _clientFd << ": " << strerror(errno) << std::endl;
        }
        _clientFd=-1;
    }
}
void HttpConnection::handleRead(){
    int savedErrno=0;
    ssize_t n=_inBuffer.readFd(_clientFd,&savedErrno);
    if(n>0){
        ssize_t readable=_inBuffer.readableBytes();
        const char* data_ptr=_inBuffer.begin()+_inBuffer.prependableBytes();
        _outBuffer.append(data_ptr,readable);
        _inBuffer.retrieve(readable);
        if(_outBuffer.readableBytes()>0){
            updateEvents(EPOLLIN | EPOLLOUT | EPOLLET);
        }
    }else if(n==0){
        handleClose();
    }else{
        if(savedErrno==EAGAIN||savedErrno==EWOULDBLOCK){
            return;
        }else{
            std::cerr << "Fatal read error on FD " << _clientFd << ": " << strerror(savedErrno) << std::endl;
            handleClose();
        }
    }
}
void HttpConnection::handleClose(){
    if(_close_callback){
        _close_callback(_clientFd,0);
    }
    closeConnection();
}
void HttpConnection::handleWrite(){
    if(_outBuffer.readableBytes()==0)return ;
    size_t data_to_send=_outBuffer.readableBytes();
    ssize_t bytes_written=0;
    while(data_to_send>0){
        const char* write_ptr=_outBuffer.begin()+_outBuffer.prependableBytes();
        size_t len=data_to_send;
        ssize_t written_now=::write(_clientFd,write_ptr,len);
        if(written_now<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK)break;
            std::cerr << "Fatal write error on FD " << _clientFd << ": " << strerror(errno) << std::endl;
            handleClose();
            return;
        }
        _outBuffer.retrieve(written_now);
        data_to_send-=written_now;
        bytes_written+=written_now;
    }
    if(_outBuffer.readableBytes()==0){
        updateEvents(EPOLLIN | EPOLLET);
    }
}
void HttpConnection::updateEvents(uint32_t events){
    if(_mod_callback){
        _mod_callback(_clientFd,events);
    }
}