#include "config.h"
#include "webserver.h"
#include <string>

using namespace std;
int main(int argc, char* argv[]) {
    //数据库信息：登录名、密码、库名
    string user = "root";
    string passwd = "root";
    string database_name = "webserver-db";

    //命令行解析
    Config config;
    config.prase_arg(argc, argv);

    //webserver服务
    WebServer server;

    //初始化
    server.init(config.port_, user, passwd, database_name, 
    config.log_write_, config.opt_linger_, config.trig_mode_, 
    config.sql_num_, config.thread_num_, config.close_log_, 
    config.actor_model_);

    //日志
    server.log_write();

    //数据库连接池
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}
