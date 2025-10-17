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
    std::vector<std::thread> _workers;//管理所有工作线程对象
    std::queue<std::function<void()>> _tasks;//工作队列
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
    void enqueue(F&& f);
};
template<class F>void ThreadPool::enqueue(F&& f){
    {
        std::lock_guard<std::mutex>lock(_queue_mutex);//换用更轻量级的lock_guard锁
        _tasks.emplace(std::forward<F>(f));
    }
    _condition.notify_one();
}
#endif