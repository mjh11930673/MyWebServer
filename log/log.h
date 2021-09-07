#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <cstring>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;



class Log {
private:
    // 1. 成员变量
    char m_dir_name[128];               // 1.1 路径名
    char m_log_name[128];               // 1.2 log文件名
    FILE* m_fp;                         // 1.3 打开的日志文件的文件描述符
    char* m_buf;                        // 1.4 日志缓冲区
    int m_buf_size;                     // 1.5 日志缓冲区的大小
    int m_max_line;                     // 1.6 日志的最大行数
    int m_line_count;                   // 1.7 日志行数的记录
    int m_today;                        // 1.8 记录当前是哪一天
    Block_Queue<string>* m_log_queue;   // 1.9 日志队列
    bool m_is_async;                    // 1.10 是否同步标志位
    Mutex m_mutex;                      // 1.11 互斥锁


private:
    // 2. 单例模式：构造函数为私有
    Log() : m_fp(NULL), m_buf(NULL), m_buf_size(0), m_max_line(0), 
    m_line_count(0), m_today(0), m_log_queue(NULL), m_is_async(false) {}
    Log(const Log&){}

    // 3. 往日志文件中写入日志
    void async_write_log();


public:
    // 4. 析构函数
    ~Log() {
        if (m_buf) delete[] m_buf;
        if (m_is_async && m_log_queue != NULL) delete[] m_log_queue;
        if (m_fp != NULL) fclose(m_fp);
    }


    // 5. 实例化该对象
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }


    // 6. 初始化
    bool init(const char* file_name, int log_buf_size = 8192, int max_lines = 5000000, int max_queue_size = 0);


    // 7. 写日志
    void write_log(int level, const char* format, ...);


    // 8. 线程回调函数
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
        return NULL;
    }
};


#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)


#endif