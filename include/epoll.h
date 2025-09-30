#include<sys/epoll.h>
#include<unistd.h>
#include<vector>
class Epoll{
private:
    int _epollfd=-1;//存储epoll实例的文件描述符
    std::vector<epoll_event> _events;//存储epoll的就绪事件数组
    static const int MAX_EVENTS=1024;
public:
    Epoll();
    ~Epoll();
    void add_fd(int fd,uint32_t events);//将socket注册到epoll实例中
    int wait(int timeout_ms=-1);
    const std::vector<epoll_event>& get_events() const { return _events; }
};