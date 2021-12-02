#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../lock/locker.h"
#include <sys/time.h>

template<typename T>
class block_queue
{
public:
    block_queue(int capacity = 1000) 
    {
        if(capacity <= 0) {
            exit(-1);
        }
        capacity_ = capacity;
        array_ = new T[max_size];
        size_ = 0;
        front_ = -1;
        back_ = -1;
    }

    ~block_queue()
    {
        mutex_.lock();
        if(array_ != NULL) {
            delete [] array_;
        }
        mutex_.unlock();
    }

    void clear()
    {
        mutex_.lock();
        size_ = 0;
        front_ = -1;
        back_ = -1;
        mutex_.unlock();
    }

    bool full()
    {
        mutex_.lock();
        if(size_ >= capacity_) {
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    bool empty()
    {
        mutex_.lock();
        if(size_ == 0) {
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    bool front(T &value)
    {
        mutex_.lock();
        if(size_ == 0) {
            mutex_.unlock();
            return false;
        }
        value = array_[front_];
        mutex_.unlock();
        return true;
    }

    bool back(T &value)
    {
        mutex_.lock();
        if(size_ == 0) {
            mutex_.unlock();
            return false;
        }
        value = array_[back_];
        mutex_.unlock();
        return true;
    }

    int size()
    {
        int tmp = 0;
        mutex_.lock();
        tmp = size_;
        mutex_.unlock();
        return tmp;
    }

    int capacity()
    {
        int tmp = 0;
        mutex_.lock();
        tmp = capacity_;
        mutex_.unlock();
        return tmp;
    }

    bool push(const T &item)
    {
        mutex_.lock();
        if(size_ >= capacity_)
        {
            cond_.broadcast();
            mutex_.unlock();
            return false;
        }
        back_ = (back_ + 1) % capacity_;
        array_[back_] = item;
        size_++;
        m_cond.broadcast();
        mutex_.unlock();
    }

    bool pop(T &item)
    {
        mutex_.lock();
        while(size_ <= 0) {
            if(!cond_.wait(mutex_.get())) {
                mutex_.unlock();
                return false;
            }
        }

        front_ = (front_ + 1) % capacity_;
        item = array_[front_];
        size_--;
        mutex_.unlock();
        return true;
    }

    bool pop(T &item, int ms_tinmeout)
    {
        timespec t = {0, 0};
        timeval now = {0, 0};
        gettimeofday(now, NULL);
        mutex_.lock();
        if(size_ <= 0) {
            t.tv_sec = now.tv_sec + ms_tinmeout / 1000;
            t.tv_nsec = (now.tv_usec + ms_timeout % 1000) * 1000;
            if(!cond_.timewait(mutex_.get(), t)) {
                mutex_.unlock();
                return false;
            }
        }

        if(size_ <= 0) {
            mutex_.unlock();
            return false;
        }

        front_ = (front_ + 1) % capacity_;
        item = array_[front_];
        size_--;
        mutex_.unlock();
        return true;
    }

private:
    locker mutex_;
    cond cond_;
    T *array_;
    int capacity_;
    int size_;
    int front_;
    int back_;
};

#endif