#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<string>
#include<cstdint>
#include<fcntl.h>
class Socket{
private:
    int _sockfd;//socket文件描述符
    sockaddr_in _serv_addr{};//存储IP地址，端口号，地址簇等信息
public:
    Socket();
//调用socket系统调用，创建socket描述符
    Socket(const Socket&)=delete;//显式禁用拷贝构造运算符和拷贝赋值运算符
    Socket& operator=(const Socket&)=delete;
    ~Socket();//析构函数
    void bind(const std::string& ip,uint16_t port);//配置服务器地址结构，调用bind()系统调用
    void listen(int backlog);//监听
    int fd()const;
    int accept(struct sockaddr_in& client_addr);
    static void set_nonblocking(int fd);
};