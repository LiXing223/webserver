#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "../lock/locker.h"
#include <pthread.h>
#include <queue>

template<typename T>
class threadpool
{
public:

    threadpool(int actor_model, )

private:
    static void *worker(void *arg);
    void run();

private:
    int max_request_;
    int thread_number_;
    pthread_t *threads_;
    std::queue<T*> request_queue_;
    locker queuelocker_;
    sem queuestat_;
};

#endif