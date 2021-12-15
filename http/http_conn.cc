#include "http_conn.h"
#include "../log/log.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/mman.h>

using namespace std;

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

void http_conn::close_conn(bool real_close)
{
    if(real_close && (sockfd_ != -1)) {
        printf("close %d/n", sockfd_);
        removefd(epollfd_, sockfd_);
        sockfd_ = -1;
        --user_count_;
        //?此处不关闭连接套接字吗？
    }
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        modfd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(epollfd_, sockfd_, EPOLLOUT, trig_mode_);
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if(read_idx_ >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据
    if(0 == trig_mode_) {
        bytes_read = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
        read_idx_ += bytes_read;
        if(bytes_read <= 0) {
            return false;
        }
        return true;
    }
    //ET读取数据
    else {
        while(true) {
            bytes_read = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
            if(bytes_read == -1) {
                if(errno == EAGAIN || errno == EOWNERDEAD) {
                    break;
                }
                return false;
            }
            else if(bytes_read == 0) {
                return false;
            }
            read_idx_ += bytes_read;
        }
        return true;
    }
}

bool http_conn::write()
{
    if(bytes_to_send_ == 0) {
        modfd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
        init();
        return true;
    }

    int temp = 0;
    while(true) {
        temp = writev(sockfd_, iv_, iv_count_);
        if(temp < 0) {
            if(errno == EAGAIN) {
                modfd(epollfd_, sockfd_, EPOLLOUT, trig_mode_);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;
        //lixing注释：此处m_iv[0].iov_len应该是write_idx_吗？
        if(bytes_have_send_ >= iv_[0].iov_len) {
            iv_[0].iov_len = 0;
            iv_[1].iov_base = file_address_ + (bytes_have_send_ - write_idx_);
            iv_[1].iov_len = bytes_to_send_;
        }
        else {
            iv_[0].iov_base = write_buf_ + bytes_have_send_;
            //lixing注释：此处m_iv[0].iov_len应该是write_idx_吗？
            iv_[0].iov_len = iv_[0].iov_len - bytes_have_send_;
        }
        if(bytes_to_send_ <= 0) {
            unmap();
            modfd(epollfd_, sockfd_, EPOLLIN, trig_mode_);
            //lixing注释：此处判断是什么意思？
            if(linger_) {
                init();
                return true;
            }
            else {
                return false;
            }
        }
    }
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
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
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
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while((check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) 
    || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        start_line_ = check_idx_;
        LOG_INFO("%s", text);
        switch (check_state_)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {        
            ret = parse_headers(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            else if(ret == GET_REQUEST) {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if(ret == GET_REQUEST) {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

bool http_conn::process_write(http_conn::HTTP_CODE ret)
{
    switch(ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)) {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form)) {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)) {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(file_stat_.st_size != 0) {
            add_headers(file_stat_.st_size);
            iv_[0].iov_base = write_buf_;
            iv_[0].iov_len = write_idx_;
            iv_[1].iov_base = file_address_;
            iv_[1].iov_len = file_stat_.st_size;
            iv_count_ = 2;
            bytes_to_send_ = write_idx_ + file_stat_.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string)) {
                return false;
            }
        }
        break;
    }
    default:
        return false;
    }
    iv_[0].iov_base = write_buf_;
    iv_[0].iov_len = write_idx_;
    iv_count_ = 1;
    bytes_to_send_ = write_idx_;
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //strpbrk检索字符串 str1 中第一个匹配字符串 str2 中字符的字符，不包含空结束字符
    url_ = strpbrk(text, " \t");
    if(!url_) {
        return BAD_REQUEST;
    }
    *url_++ = '0';
    char *method = text;
    //strcasecmp判断字符串是否相等(忽略大小写)
    if(strcasecmp(method, "GET") == 0) {
        method_ = GET;
    }
    else if(strcasecmp(method, "POST") == 0) {
        method_ = POST;
        cgi_ = 1;
    }
    else {
        return BAD_REQUEST;
    }
    //trspn检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    url_ += strspn(url_, " \t");
    version_ = strpbrk(url_, " \t");
    if(!version_) {
        return BAD_REQUEST;
    }
    *version_++ = '\0';
    version_ += strspn(version_, " \t");
    if(strcasecmp(version_, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    //strcasecmp判断字符串前n个字符是否相等(忽略大小写)
    if(strncasecmp(url_, "http://", 7) == 0) {
        url_ += 7;
        //strchr在 str字符串中搜索第一次出现字符 c（一个无符号字符）的位置
        url_ = strchr(url_, '/');
    }
    if (strncasecmp(url_, "https://", 8) == 0)
    {
        url_ += 8;
        url_ = strchr(url_, '/');
    }

    if(url_ || url_[0] != '/') {
        return BAD_REQUEST;
    }
    //当url为/时，显示判断界面
    if(strlen(url_) == 1) {
        strcat(url_, "judge.html");
    }
    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if(text[0] == '\0') {
        if(content_length_ != 0) {
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            linger_ = true;
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        content_length_ = atoi(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    }
    else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if(read_idx_ >= (content_length_ + check_idx_)) {
        text[content_length_] = '\0';
        //post请求中最后为输入的用户名和密码
        string_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(real_file_, doc_root_);
    int len = strlen(doc_root_);
    //printf("url:%s\n", url_);
    //所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置
    const char *p = strrchr(url_, '/');

    //lixing注释：此处是否有问题？
    char url_real[200]; 
    //处理cgi
    if(cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = url_[1];

        strcpy(url_real, "/");
        strcat(url_real, url_ + 2);
        strncpy(real_file_ + len, url_real, FILENAME_LEN - len - 1);

        //将用户名和密码提取出来
        char name[100], password[100];
        int i;
        for(i = 5; string_[i] != '&'; ++i) {
            name[i - 5] = string_[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for(i = i + 10; string_[i] != '\0'; ++i, ++j) {
            password[j] = string_[i];
        }
        password[j] = '\0';

        if(*(p + 1) == '3') {
            //如果是注册，先检测数据库中是否有重名
            //如果没有重名，插入数据
            if(users.find(name) == users.end()) {
                char *sql_insert = (char *)malloc(sizeof(char) * 200);
                sprintf(sql_insert, "INSERT INTO user(username, passwd) VALUES(\'%s\', \'%s\')", name, password);
                lock_.lock();
                int res = mysql_query(mysql_, sql_insert);
                if(!res) {
                    strcpy(url_, "/log.html");
                }
                else {
                    strcpy(url_, "/registerError.html");
                }
            }
            else {
                strcpy(url_, "/registerError.html");
            }
        }
        else if(*(p + 1) == '2') {
            //如果是登录，直接判断
            //若浏览器端输入的用户名和密码再表中可以查找到，返回1，否则返回0
            if(users.find(name) != users.end() && users[name] == password) {
                strcpy(url_, "/welcome.html");
            }
            else {
                strcpy(url_, "/logError.html");
            }
        }
    }

    if(*(p + 1) == '0') {
        strcpy(url_real, "register.html");
        strncpy(real_file_ + len, url_real, strlen(url_real));
    }
    else if(*(p + 1) == '1') {
        strcpy(url_real, "log.html");
        strncpy(real_file_ + len, url_real, strlen(url_real));
    }
    else if(*(p + 1) == '5') {
        strcpy(url_real, "picture.html");
        strncpy(real_file_ + len, url_real, strlen(url_real));
    }
    else if(*(p + 1) == '6') {
        strcpy(url_real, "register.html");
        strncpy(real_file_ + len, url_real, strlen(url_real));
    }
    else if(*(p + 1) == '7') {
        strcpy(url_real, "fans.html");
        strncpy(real_file_ + len, url_real, strlen(url_real));
    }
    else {
        strncpy(real_file_ + len, url_, FILENAME_LEN - len - 1);
    }

    if(stat(real_file_, &file_stat_) < 0) {
        return NO_RESOURSE;
    }
    if(!(file_stat_.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(file_stat_.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(real_file_, O_RDONLY);
    file_address_ = (char*)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; check_idx_ < read_idx_; ++check_idx_) {
        temp = read_buf_[check_idx_];
        if(temp == '\r') {
            if((check_idx_ + 1) == read_idx_) {
                return LINE_OPEN;
            }
            else if(read_buf_[check_idx_ + 1] == '\n') {
                read_buf_[check_idx_++] = '\0';
                read_buf_[check_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if(check_idx_ > 1 && read_buf_[check_idx_ - 1] == '\r') {
                read_buf_[check_idx_ - 1] = '\0';
                read_buf_[check_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

void http_conn::unmap()
{
    if(file_address_) {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if(write_idx_ >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(write_buf_ + write_idx_, 
    WRITE_BUFFER_SIZE - 1 - write_idx_, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - write_idx_)) {
        va_end(arg_list);
        return false;
    }
    write_idx_ += len;
    va_end(arg_list);

    LOG_INFO("request:%s", write_buf_);

    return true;
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title); 
}

bool http_conn::add_headers(int content_length)
{
    return add_content_length(content_length) 
    && add_linger() && add_blank_line();
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length:%d\r\n", content_length); 
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n",
        linger_ == true ? "keep-alive" : "close"); 
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}