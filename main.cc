#include "config.h"
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
    

    return 0;
}
