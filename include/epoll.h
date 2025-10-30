#ifndef HTTPCCC_EPOLL_H 
#define HTTPCCC_EPOLL_H 
#include<sys/epoll.h>
#include<unistd.h>
#include<vector>
class Epoll{
private:
    int _epollfd=-1;
    std::vector<epoll_event> _events;
    static const int MAX_EVENTS=1024;
public:
    Epoll();
    Epoll(const Epoll&)=delete;
    Epoll &operator=(const Epoll&)=delete;
    ~Epoll();
    void add_fd(int fd,uint32_t events);
    int wait(int timeout_ms=-1);
    const std::vector<epoll_event>& get_events() const { return _events; }
    void mod_fd(int fd,uint32_t events);
    void del_fd(int fd);
};
#endif