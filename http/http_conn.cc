#include "http_conn.h"
#include "../log/log.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker lock_;
std::map<std::string, std::string> users;

//对文件描述符设置为非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode) {
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

//将内核事件表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);  //关闭文件描述符
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if(1 == TRIGMode) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; 
    }
    else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP; 
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::user_count_ = 0;
int http_conn::epollfd_ = -1;

void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
    int close_log, std::string user, std::string passwd, std::string sqlname)
{
    sockfd_ = sockfd;
    address_ = addr;

    //当浏览器出现连接重置时，可能是网站的根目录出错或http响应格式出错或者访问的文件内容完全为空
    doc_root_ = root;
    trig_mode_ = TRIGMode;
    close_log_ = close_log;

    addfd(epollfd_, sockfd_, true, trig_mode_);
    ++user_count_;

    strcpy(sql_user_, user.c_str());
    strcpy(sql_passwd_, passwd.c_str());
    strcpy(sql_name_, sqlname.c_str());

    init();
}

void http_conn::close_conn(bool real_close = true)
{

}

void http_conn::process()
{

}

bool http_conn::read_once()
{

}

bool http_conn::write()
{

}

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取出一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if(mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql_));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    
    //返回结果集的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字符结构的数组
    

}

void http_conn::init()
{
    timer_flag_ = 0;
    improv_ = 0;
    mysql_ = NULL; 
    state_ = 0;
    read_idx_ = 0;
    check_idx_ = 0;
    start_line_ = 0;
    write_idx_ = 0;
    check_state_ = CHECK_STATE_REQUESTLINE;
    method_ = GET;
    url_ = NULL;
    version_ = NULL;
    content_length_ = 0;
    host_ = NULL;
    linger_ = false;
    //char file_address_;       
    //struct stat file_stat_;
    //struct iovec iv_[2];
    //int iv_count_;
    cgi_ = 0;
    string_ = NULL;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    memset(read_buf_, '\0', READ_BUFFER_SIZE);
    memset(write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(real_file_, '\0', FILENAME_LEN);
}

http_conn::HTTP_CODE http_conn::process_read()
{

}

bool http_conn::process_write(http_conn::HTTP_CODE ret)
{

}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{

}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{

}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{

}

http_conn::HTTP_CODE http_conn::do_request()
{

}

http_conn::LINE_STATUS http_conn::parse_line()
{

}

void http_conn::unmap()
{

}

bool http_conn::add_response(const char *format, ...)
{

}

bool http_conn::add_content(const char *content)
{

}

bool http_conn::add_status_line(int status, const char *title)
{

}

bool http_conn::add_headers(int content_length)
{

}

bool http_conn::add_content_type()
{

}

bool http_conn::add_content_length(int content_length)
{

}

bool http_conn::add_linger()
{

}

bool http_conn::add_blank_line()
{

}