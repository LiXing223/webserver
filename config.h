#ifndef CONFIG_H
#define CONFIG_H

class Config
{
public:
    Config();
    ~Config(){};
    void prase_arg(int argc, char* argv[]);

public:
    int port_;              //端口号
    int log_write_;         //日志写入方式：0，同步；1，异步
    int trig_mode_;         //触发组合模式
    int listen_trig_mode_;  //listenfd触发模式
    int connect_trig_mode_; //connfd触发模式
    int opt_linger_;        //优雅关闭连接
    int sql_num_;           //数据库连接池数量
    int thread_num_;        //线程池内的线程数量
    int close_log_;         //是否关闭日志
    int actor_model_;       //(事件处理模式)并发模式选择
};

#endif