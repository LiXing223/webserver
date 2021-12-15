#include "webserver.h"
#include "./log/log.h"
#include <signal.h>
#include <string.h>
 #include <assert.h>
 #include <arpa/inet.h>

WebServer::WebServer()
{
    //http_conn类对象
    users_ = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    root_ = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(root_, server_path);
    strcat(root_, root);

    //定时器
    users_timer_ = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(epollfd_);
    close(listenfd_);
    close(pipefd_[1]);
    close(pipefd_[0]);
    delete[] users_;
    delete[] users_timer_;
    delete pool_;
    delete root_;
}

void WebServer::init(int port, string user, string passWord, 
string databaseName, int log_write, int opt_linger, int trigmode,
int sql_num, int thread_num, int close_log, int actor_model)
{
    port_ = port;
    user_ = user;
    passWord_ = passWord_;
    databaseName_ = databaseName;
    log_write_ = log_write;
    opt_linger_ = opt_linger;
    trig_mode_ = trigmode;
    sql_num_ = sql_num;
    thread_num_ = thread_num;
    close_log_ = close_log;
    actor_model_ = actor_model;
}

void WebServer::thread_pool()
{
    //线程池
    pool_ = new threadpool<http_conn>(actor_model_, connPool_, thread_num_);
}

void WebServer::trig_mode()
{
    //LT + LT
    if(0 == trig_mode_) {
        listen_trig_mode_ = 0;
        conn_trig_mode_ = 0;
    }
    //LT + ET
    else if (1 == trig_mode_) {
        listen_trig_mode_ = 0;
        conn_trig_mode_ = 1;
    }
    //ET + LT
    else if (2 == trig_mode_) {
        listen_trig_mode_ = 1;
        conn_trig_mode_ = 0;
    }
    //ET + ET
    else if (3 == trig_mode_) {
        listen_trig_mode_ = 1;
        conn_trig_mode_ = 1;
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    connPool_ = connection_pool::GetInstance();
    connPool_->init("localhost", user_, passWord_,
        databaseName_, 3306, sql_num_, close_log_);

    //初始化数据库读取表
    users_->initmysql_result(connPool_);
}

void WebServer::log_write()
{
    if(close_log_ == 0) {
        //初始化日志
        if(1 == log_write_) {
            Log::get_instance()->init("./ServerLog", close_log_, 2000, 80000, 800);
        }
        else {
            Log::get_instance()->init("./ServerLog", close_log_, 2000, 80000, 0);
        }
    }
}

void WebServer::eventListen()
{
    listenfd_ = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd_ >= 0);

    //优雅关闭连接
    if(0 == opt_linger_) {
        struct linger tmp = {0, 1};
        setsockopt(listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(1 == opt_linger_) {
        struct linger tmp = {1, 1};
        setsockopt(listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;

    struct sockaddr_in address;
    //bzero() 会将内存块（字符串）的前n个字节清零
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    int flag = 1;
    setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd_, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd_, 5);
    assert(ret >= 0);

    util_.init(TIMESLOT);

    //epoll创建内核事件表
    epollfd_ = epoll_create(5);
    assert(epollfd_ != -1);

    util_.addfd(epollfd_, listenfd_, false, listen_trig_mode_);
    http_conn::epollfd_ = epollfd_;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd_);
    assert(ret != -1);
    util_.setnonblocking(pipefd_[1]);
    util_.addfd(epollfd_, pipefd_[0], false, 0);

    util_.addsig(SIGPIPE, SIG_IGN);
    util_.addsig(SIGALRM, util_.sig_handle, false);
    util_.addsig(SIGTERM, util_.sig_handle, false);

    alarm(TIMESLOT);

    //工具类
    Utils::u_pipefd_ = pipefd_;
    Utils::u_epollfd_ = epollfd_;
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    while(!stop_server) {
        int number = epoll_wait(epollfd_, events_, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0; i < number; ++i) {
            int sockfd = events_[i].data.fd;
            //处理新到的客户连接
            if(events_[i].data.fd == listenfd_) {
                bool flag = dealclientdata();
                if(false == flag) {
                    continue;
                }
            }
            else if(events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer_[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if((sockfd == pipefd_[0]) && (events_[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                if(false == flag) {
                    LOG_ERROR("%s", "dealwithsignal failure");
                }
            }
            //处理客户端连接上接收到的数据
            else if(events_[i].events & EPOLLIN) {
                dealwithread(sockfd);
            }
            else if(events_[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
    }
}

bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if(0 == listen_trig_mode_) {
        int connfd = accept(listenfd_, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::user_count_ >= MAX_FD) {
            util_.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else {
        while(true) {
            int connfd = accept(listenfd_, (struct sockaddr*)&client_address, &client_addrlength);
            if(connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                return false;
            }
            if(http_conn::user_count_ >= MAX_FD) {
                util_.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                return false;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) 
{
    users_[connfd].init(connfd, client_address, root_, conn_trig_mode_,
                        close_log_, user_, passWord_, databaseName_);
    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表
    users_timer_[connfd].address = client_address;
    users_timer_[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data_ = &users_timer_[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire_ = cur + 3 * TIMESLOT;
    users_timer_[connfd].timer = timer;
    util_.timer_lst_.add_timer(timer);
}

void WebServer::adjust_timer(util_timer *timer)
{

}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer_[sockfd]);
    if(timer) {
        util_.timer_lst_.delete_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer_[sockfd].sockfd);
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(pipefd_[0], signals, sizeof(signals), 0);
    if(ret == -1) {
        return false;
    }
    else if(ret == 0) {
        return false;
    }
    else {
        for(int i = 0; i < ret; ++i) {
            switch(signals[i]) 
            {
            case SIGALRM:
            {
                timeout = true;
                break;  //lixing注释：此处为什么break，如果break会遗漏程序终止信号
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer_[sockfd].timer;

    //reactor
    if(1 == actor_model_) {
        if(timer) {
            adjust_timer(timer);
        }
        //若检测到写事件，将事件放入请求队列
        pool_->append(users_ + sockfd, 1);
        while(true) {
            if(1 == users_[sockfd].improv_) {
                if(1 == users_[sockfd].timer_flag_) {
                    deal_timer(timer, sockfd);
                    users_[sockfd].timer_flag_ = 0;
                }
                users_[sockfd].improv_ = 0;
                break;
            }
        }
    }
    //proactor
    else {
        if(users_[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", 
            inet_ntoa(users_[sockfd].get_address()->sin_addr));
            //若监测到读事件，就该事件放入请求队列
            pool_->append_p(users_ + sockfd);
            if(timer) {
                adjust_timer(timer);
            }
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
        util_timer *timer = users_timer_[sockfd].timer;

    //reactor
    if(1 == actor_model_) {
        if(timer) {
            adjust_timer(timer);
        }
        //若检测到读事件，将事件放入请求队列
        pool_->append(users_ + sockfd, 0);
        while(true) {
            if(1 == users_[sockfd].improv_) {
                if(1 == users_[sockfd].timer_flag_) {
                    deal_timer(timer, sockfd);
                    users_[sockfd].timer_flag_ = 0;
                }
                users_[sockfd].improv_ = 0;
                break;
            }
        }
    }
    //proactor
    else {
        if(users_[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", 
            inet_ntoa(users_[sockfd].get_address()->sin_addr));
            if(timer) {
                adjust_timer(timer);
            }
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}