#include "mysql_connection_pool.h"




// 1. 单例模式
Connection_Pool* Connection_Pool::getInstance() {
    static Connection_Pool conn_pool;
    return &conn_pool;
}


// 2. 初始化
void Connection_Pool::init(string url, string user, string pw, string dbname, int dbport, unsigned int maxconn) {
    m_url = url;
    m_db_port = dbport;
    m_db_name = dbname;
    m_user = user;
    m_password = pw;
    MaxConn = maxconn;

    m_mutex.lock();
    for (int i = 0; i < maxconn; ++i) {
        MYSQL* mysql = mysql_init(NULL);
        if (mysql == NULL) {
            LOG_ERROR("%dth mysql_init() is error", i);
            continue;
        }

        mysql = mysql_real_connect(mysql, m_url.c_str(), m_user.c_str(), m_password.c_str(), 
                                    m_db_name.c_str(), m_db_port, NULL, 0);
        if (mysql == NULL) {
            LOG_ERROR("%dth mysql_real_connect() is error", i);
            continue;
        }

        m_connList.push_back(mysql);
        ++FreeConn;
    }

    LOG_INFO("create connection pool num: %d, failed num: %d", FreeConn, maxconn - FreeConn);

    m_sem = Sem(FreeConn);
    m_mutex.unlock();
}


// 3. 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* Connection_Pool::getConnection() {
    if ((m_connList.size() == 0) || (FreeConn == 0)) return NULL;

    m_sem.wait_sem();
    m_mutex.lock();

    MYSQL* mysql = m_connList.front();
    m_connList.pop_front();

    --FreeConn;
    ++UseConn;

    m_mutex.unlock();
    return mysql;
}


// 4. 释放当前使用的连接
bool Connection_Pool::releaseConnection(MYSQL* conn) {
    if (conn == NULL) return false;

    m_mutex.lock();

    m_connList.push_back(conn);
    ++FreeConn;
    --UseConn;

    m_mutex.unlock();
    m_sem.post_sem();

    return true;
}



// 5. 销毁所有的数据库连接池
void Connection_Pool::destroyConnPool() {
    m_mutex.lock();

    if (m_connList.size() > 0) {
        for (auto mysql : m_connList) {
            mysql_close(mysql);
        }

        FreeConn = 0;
        UseConn = 0;

        m_connList.clear();
    }

    m_mutex.unlock();
}
