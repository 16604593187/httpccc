#include<sys/epoll.h>
#include<unistd.h>
#include<vector>
class Epoll{
private:
    int _epollfd=-1;//存储epoll实例的文件描述符
    std::vector<epoll_event> _events;//存储epoll的就绪事件数组
    static const int MAX_EVENTS=1024;//限定_events最大值
public:
    Epoll();
    Epoll(const Epoll&)=delete;
    Epoll &operator=(const Epoll&)=delete;
    ~Epoll();
    void add_fd(int fd,uint32_t events);//将socket注册到epoll实例中
    int wait(int timeout_ms=-1);//返回等待确认的事件个数
    const std::vector<epoll_event>& get_events() const { return _events; }
    void mod_fd(int fd,uint32_t events);//事件切换
    void del_fd(int fd);//描述符移除
};