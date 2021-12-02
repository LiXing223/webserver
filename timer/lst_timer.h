#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <netinet/in.h>

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {} 

public:
    time_t expire_;
    void (* cb_func)(client_data *);
    client_data *user_data_;
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void delete_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

private:
    util_timer *head_;
    util_timer *tail_;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    int setnonblocking(int fd);

    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    static void sig_handle(int sig);

    void addsig(int sig, void(handler)(int), bool restart = true);

    void timer_handle();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd_;
    sort_timer_lst timer_lst_;
    static int u_epollfd_;
    int timeslot_;
};

void cd_func(client_data *user_data);

#endif