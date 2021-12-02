#ifndef LOG_H
#define LOG_H

#include "../lock/locker.h"
#include "block_queue.h"
#include <stdio.h>
#include <string>

class Log 
{
public:
    static Log *get_instance()
    {
        //C++11以后，懒汉式使用局部静态变量不用加锁
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    bool init(const char *file_name, int close_log, int log_buf_size = 8192, 
        int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    ~Log();
    void *async_write_log()
    {
        std::string single_log;
        while(log_queue_->pop(single_log)) {
            mutex_.lock();
            fputs(single_log.c_str(), fp_);
            mutex_.unlock();
        }
    }


private:
    char dir_name_[128];    //日志路径
    char log_name_[128];    //log文件名
    int split_lines_;       //日志最大行数
    int log_buf_size_;      //日志缓冲区大小
    long long count_;       //日志行数纪录
    int today_;             //因为按天分类，记录当前时间是哪天
    FILE *fp_;              //打开log的文件指针
    char *buf_;             //日志缓冲区
    block_queue<std::string> *log_queue_;
    bool is_async_;
    locker mutex_;
    int close_log_;
};

#define LOG_DEBUG(format, ...) if(0 == close_log_) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == close_log_) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == close_log_) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == close_log_) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif