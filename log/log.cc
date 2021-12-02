#include "log.h"
#include <string.h>
#include <stdarg.h>

using namespace std;

Log::Log()
{
    count_ = 0;
    is_async_ = false;
}

Log::~Log()
{
    if(fp_ != NULL) {
        fclose(fp_);
    }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, 
    int split_lines, int max_queue_size)
{
    if(max_queue_size >= 1) {
        is_async_ = true;
        log_queue_ = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    
    close_log_ = close_log_;
    log_buf_size_ = log_buf_size;
    buf_ = new char[log_buf_size_];
    memset(buf_, '\0', log_buf_size_);
    split_lines_ = split_lines;

    time_t t = time(NULL);
    tm *sys_tm = localtime(&t);
    tm my_tm = *sys_tm;

    //strrchr : Locate last occurrence of character in string
    const char *p = strrchr(file_name, '/'); 
    char log_full_name[256] = {0};

    if(p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
            my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        strcpy(log_name_, p + 1);
        strncpy(dir_name_, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name_, my_tm.tm_year + 1900,
            my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }

    today_ = my_tm.tm_mday;

    fp_ = fopen(log_full_name, "a");
    return fp_ == NULL;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    tm *sys_tm = localtime(&t);
    tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level) 
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log
    mutex_.lock();
    count_++;
    if(today_ != my_tm.tm_mday || count_ % split_lines_ == 0) {
        char new_log[256] = {0};
        fflush(fp_);
        fclose(fp_);
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, 
            my_tm.tm_mon + 1, my_tm.tm_mday);
        if(today_ != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name_, tail, log_name_);
            today_ = my_tm.tm_mday;
            count_ = 0; 
        }
        else {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name_,
             tail, log_name_, count_ / split_lines_);
        }
        fp_ = fopen(new_log, "a");
    }

    mutex_.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    mutex_.lock();

    int n = snprintf(buf_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
            my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //修改：log_buf_size_ - 1 改为 log_buf_size_ - n - 2
    int m = snprintf(buf_ + n, log_buf_size_ - n - 2, format, valst);   
    buf_[n + m] = '\n';
    buf_[n + m + 1] = '\0';
    log_str = buf_;

    mutex_.unlock();

    //1. 如果阻塞队列满了？当前是直接输出到文件，是否合理？导致日志不是按时间顺序输出
    //2. count_计数日志文件的行数，count_计数值改变是否应在写入文件之后？当前不合理？
    if(is_async_ && !log_queue_->full()) {
        log_queue_->push(log_str);
    }
    else {
        mutex_.lock();
        fputs(log_str.c_str(), fp_);
        mutex_.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    mutex_.lock();
    //强制刷新写入流缓冲区
    fflush(fp_);
    mutex_.unlock();
}