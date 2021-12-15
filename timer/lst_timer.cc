#include "lst_timer.h"
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>

sort_timer_lst::sort_timer_lst()
{
    head_ = NULL;
    tail_ = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head_;
    while(tmp) {
        head_ = tmp->next;
        delete tmp;
        tmp = head_;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if(!timer) return;
    if(!head_) {
        head_ = tail_ = timer;
        return;
    }
    if(timer->expire_ < head_->expire_) {
        timer->next = head_;
        head_->prev = timer;
        head_ = timer;
        return;
    }
    add_timer(timer, head_);
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer) {
        return;
    }
    util_timer *tmp = timer->next;
    if(!tmp || timer->expire_ < tmp->expire_) {
        return;
    }
    if(timer == head_) {
        head_ = head_->next;
        head_->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head_);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::delete_timer(util_timer *timer)
{
    if(!timer) {
        return;
    }
    if((timer == head_) && (timer == tail_)) {
        delete timer;
        head_ = NULL;
        tail_ = NULL;
        return;
    }
    if(timer == head_) {
        head_ = head_->next;
        head_->prev = NULL;
        delete timer;
    }
    if(timer == head_) {
        head_ = head_->next;
        head_->prev = NULL;
        delete timer;
        return;
    }
    if(timer == tail_) {
        tail_ = tail_->prev;
        tail_->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick()
{
    if(!head_) {
        return;
    }

    time_t cur = time(NULL);
    util_timer *tmp = head_;
    while(head_) {
        if(cur < tmp->expire_) {
            break;
        }
        tmp->cb_func(tmp->user_data_);
        head_ = tmp->next;
        head_ ? (head_->prev = NULL) : (tail_ = NULL);
        delete tmp;
        tmp = head_;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *cur = prev->next;
    while(cur) {
        if(timer->expire_ < cur->expire_) {
            prev->next = timer;
            cur->prev = timer;
            timer->next = cur;
            timer->prev = prev;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    if(!cur) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail_ = timer;
    }
}

void Utils::init(int timeslot)
{
    timeslot_ = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handle(int sig)
{
    int save_error = errno;
    int msg = sig;
    send(u_pipefd_[1], (char *)&msg, 1, 0);
    errno = save_error;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handle()
{
    timer_lst_.tick();
    alarm(timeslot_);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd_ = 0;
int Utils::u_epollfd_ = 0;

void cb_func(client_data *user_data)
{
    assert(user_data);  //lix添加
    epoll_ctl(Utils::u_epollfd_, EPOLL_CTL_DEL, user_data->sockfd, 0);
    //assert(user_data);    //lix注释
    close(user_data->sockfd);
    //http_conn::user_count_--;
}
