#include "ThreadPool.h"
ThreadPool::ThreadPool(size_t num_threads):_stop(false){
    for(size_t i=0;i<num_threads;++i){
        _workers.emplace_back([this]{
            while(true){
                std::function<void()>task;{
                    std::unique_lock<std::mutex>lock(_queue_mutex);
                    _condition.wait(lock,[this]{
                        return _stop||!_tasks.empty();//队列不为空或者_stop为true时才会停止等待。
                    });
                
                if(_stop&&_tasks.empty())return;
                task=std::move(_tasks.front());
                _tasks.pop();
                }
                task();
            }
            });
    }
}
ThreadPool::~ThreadPool(){
    {
        std::unique_lock<std::mutex>lock(_queue_mutex);
        _stop=true;
    }
    _condition.notify_all();
    for(std::thread& worker:_workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}