#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include "../lock/locker.h"
#include <mysql/mysql.h>
#include <list>
#include <string>

class connection_pool
{
private:
	connection_pool();
	~connection_pool(); 

public:
    //单例模式
    static connection_pool *GetInstance();

public:
    void init(
        std::string url, 
        std::string user, 
        std::string password, 
        std::string db_name, 
        int port,
        int max_conn,
        int close_log); 
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *con);
    int GetFreeConn();
    void DestroyPool();
    
private:
    int max_conn_;
    int cur_conn_;
    int free_conn_;
    locker lock_;
    std::list<MYSQL *> conn_list_;
    sem reserve_;

public:
    std::string url_;
    std::string port_;
    std::string user_;
    std::string password_;
    std::string db_name_;
    int close_log_;
};

class connectionRAII
{
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *connRAII;
    connection_pool *poolRAII;
};

#endif