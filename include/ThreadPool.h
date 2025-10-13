#ifndef HTTPCCC_THREADPOOL_H
#define HTTPCCC_THREADPOOL_H
#include<vector>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<functional>
class ThreadPool{
private:
    std::vector<std::thread> _thread;//管理所有工作线程对象
    std::queue<std::function<void()>> _task;//工作队列
    std::mutex _queue_mutex;
    std::condition_variable _condition;//条件变量，协调工作线程与主线程通信与同步
    bool _stop;
public:
    ThreadPool(size_t num_threads);
    ThreadPool(const ThreadPool&)=delete;
    ThreadPool operator=(const ThreadPool&)=delete;
    ThreadPool(ThreadPool&&)=delete;
    ThreadPool operator=(ThreadPool&&)=delete;
    ~ThreadPool();
    template<class F>
    void equeue(F&& f);
};
#endif