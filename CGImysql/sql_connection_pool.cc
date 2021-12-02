#include "sql_connection_pool.h"
#include "../log/log.h"

connection_pool::connection_pool()
{
    cur_conn_ = 0;
    free_conn_ = 0;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connection_pool *connection_pool::GetInstance()
{
    static connection_pool conn_pool;
    return &conn_pool;
}
    
void connection_pool::init(
    std::string url, 
    std::string user, 
    std::string password, 
    std::string db_name, 
    int port,
    int max_conn,
    int close_log)
{
    url_ = url;
    port_ = port;   //注意：string的operator=重载了char，所以可以直接赋值
    user_ = user;
    password_ = password;
    db_name_ = db_name;
    close_log_ = close_log;

    for(int i = 0; i < max_conn; ++i) {
        MYSQL *con = NULL;
        con = mysql_init(con);
        if(con = NULL) {
            LOG_ERROR("MySQL ERROR : init");
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), user.c_str(), 
            password_.c_str(), db_name.c_str(), port, NULL, 0);
        if(con = NULL) {
            LOG_ERROR("MySQL ERROR : connect");
            exit(1);
        }

        conn_list_.push_back(con);
        ++free_conn_;
    }
    reserve_ = sem(free_conn_);
    max_conn = free_conn_;
}

MYSQL *connection_pool::GetConnection()
{
    //???是否逻辑有问题
    MYSQL *con = NULL;

    if(0 == conn_list_.size()) {
        return NULL;
    }    

    reserve_.wait();

    lock_.lock();
    
    con = conn_list_.front();
    conn_list_.pop_front();
    
    --free_conn_;
    ++cur_conn_;
    
    lock_.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if(con == NULL) {
        return false;
    }

    lock_.lock();

    conn_list_.push_back(con);
    
    ++free_conn_;
    --cur_conn_;

    lock_.unlock();
    reserve_.post();

    return true;
}

int connection_pool::GetFreeConn()
{
    return free_conn_;
}

void connection_pool::DestroyPool()
{
    lock_.lock();
    
    if(conn_list_.size() > 0) {
        for(std::list<MYSQL *>::iterator it = conn_list_.begin(); 
            it != conn_list_.end(); ++it) {
            mysql_close(*it);
        }
        cur_conn_ = 0;
        free_conn_ = 0;
        conn_list_.clear();
    }

    lock_.unlock();
}

connectionRAII::connectionRAII(MYSQL **con, connection_pool *connPool)
{
    *con = connPool->GetConnection();
    connRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII);
}



