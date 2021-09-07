#ifndef MYSQL_CONNECTION_POOL_H
#define MYSQL_CONNECTION_POOL_H

#include <stdio.h>
#include <error.h>
#include <iostream>
#include <string>
#include <cstring>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <mysql/mysql.h>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;



// 1. 连接池
class Connection_Pool {
private:
    // 1. 成员变量
    unsigned int MaxConn;       // 1.1 最大的连接数
    unsigned int UseConn;       // 1.2 当前已使用的连接数
    unsigned int FreeConn;      // 1.3 当前空闲的连接数

    list<MYSQL*> m_connList;    // 1.4 连接池
    Mutex m_mutex;              // 1.5 互斥锁
    Sem m_sem;                  // 1.6 信号量

    string m_url;               // 1.7 主机地址
    unsigned int m_db_port;     // 1.8 数据库端口号
    string m_db_name;           // 1.9 使用的数据库库名
    string m_user;              // 1.10 登陆数据库用户名
    string m_password;          // 1.11 登陆数据库密码


public:
    // 2. 与连接相关的成员函数
    MYSQL* getConnection();                     // 2.1 获取数据库连接
    bool releaseConnection(MYSQL* conn);        // 2.2 释放连接
    void destroyConnPool();                     // 2.3 销毁所有连接
    int getFreeConn() { return FreeConn; }      // 2.4 获取当前空闲的连接数

    // 3. 单例模式
    static Connection_Pool* getInstance();
    void init(string url, string user, string pw, string dbname, int dbport, unsigned int maxconn);
    ~Connection_Pool() { this->destroyConnPool(); }

private:
    // 4. 私有化的构造函数
    Connection_Pool() : MaxConn(0), UseConn(0), FreeConn(0) {}
    Connection_Pool(const Connection_Pool&) {}
};



// 2. RAII
class ConnectionRAII {
private:
    MYSQL* m_connRAII;
    Connection_Pool* m_poolRAII;

public:
    ConnectionRAII(MYSQL** conn, Connection_Pool* connPool) {
        *conn = connPool->getConnection();

        m_connRAII = *conn;
        m_poolRAII = connPool;
    }

	~ConnectionRAII() {
        m_poolRAII->releaseConnection(m_connRAII); 
    }
};



#endif
