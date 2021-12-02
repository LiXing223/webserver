#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include "../CGImysql/sql_connection_pool.h"
#include <mysql/mysql.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <map>
#include <string>

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD
    {
        GET = 0,
        POST, 
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTION,
        CONNECT, 
        PATH
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
        int close_log, std::string user, std::string passwd, std::string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address() 
    {
        return &address_; 
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag_;
    int improv_;

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() 
    {
        return read_buf_ + start_line_;
    }
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int epollfd_;
    static int user_count_;
    MYSQL *mysql_;
    int state_; //读为0,写为1


private:
    int sockfd_;
    sockaddr_in address_;
    char read_buf_[READ_BUFFER_SIZE];   //读缓冲区
    int read_idx_;                      //当前已读取了多少字节的客户数据
    int check_idx_;                     //当前已经分析完了多少字节的客户数据
    int start_line_;                    //行在buffer中的起始位置
    int write_buf_[WRITE_BUFFER_SIZE];  
    int write_idx_;
    CHECK_STATE check_state_;
    METHOD method_;
    char real_file_[FILENAME_LEN];
    char *url_;
    char *version_;
    char *host_;
    int content_length_;
    bool linger_;
    char file_address_;
    struct stat file_stat_;
    struct iovec iv_[2];
    int iv_count_;
    int cgi_;        //是否启用
    char *string_;  //存储请求头数据
    int bytes_to_send_;
    int bytes_have_send_;
    char *doc_root_;

    std::map<std::string, std::string> users_;
    int trig_mode_;
    int close_log_;

    char sql_user_[100];
    char sql_passwd_[100];
    char sql_name_[100];
};

#endif