#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include <pthread.h>
#include <queue>
#include <exception>

template<typename T>
class threadpool
{
public:
    threadpool(int actor_model, connection_pool *connPool, 
    int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    static void *worker(void *arg);
    void run();

private:
    int thread_number_;
    int max_request_;
    pthread_t *threads_;
    std::queue<T*> request_queue_;
    locker queuelocker_;
    sem queuestat_;
    connection_pool *connPool_;
    int actor_model_;
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, 
int thread_number, int max_request)
: actor_model_(actor_model)
, connPool_(connPool)
, thread_number_(thread_number)
, max_request_(max_request)
{
    if(thread_number <= 0 || max_request < 0) {
        throw std::exception();
    }
    threads_ = new pthread_t[thread_number];
    if(!threads_) {
        throw std::exception();
    }
    for(int i = 0; i < thread_number; ++i) {
        if(pthread_create(threads_ + i, NULL, worker, this) != 0) {
            delete[] threads_;
            throw std::exception();
        }
        if(pthread_detach(threads_[i]) != 0) {
            delete[] threads_;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] threads_;
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    queuelocker_.lock();
    if(request_queue_.size() >= max_request_) {
        queuelocker_.unlock();
        return false;
    }
    request->state_ = state;
    request_queue_.push(request);
    queuelocker_.unlock();
    queuestat_.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    queuelocker_.lock();
    if(request_queue_.size() >= max_request_) {
        queuelocker_.unlock();
        return false;
    }
    request_queue_.push(request);
    queuelocker_.unlock();
    queuestat_.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while(true) {
        queuestat_.wait();
        queuelocker_.lock();
        if(request_queue_.empty()) {
            queuelocker_.unlock();
            continue;
        }
        T *request = request_queue_.front();
        request_queue_.pop();
        queuelocker_.unlock();
        if(!request) {
            continue;
        }
        if(1 == actor_model_) {
            if(0 == request->state_) {
                if(request->read_once()) {
                    request->improv_ = 1;
                    connectionRAII mysqlcon(&request->mysql_, connPool_);
                    request->process();
                }
                else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
            else {
                if(request->write()) { 
                    request->improv_ = 1;
                }
                else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
        }
        else {
            connectionRAII mysqlcon(&request->mysql_, connPool_);
            request->process();
        }
    }
}

#endif