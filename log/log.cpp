#include "log.h"


// 3. 往日志文件中写入日志
void Log::async_write_log() {
    string single_log;

    while (m_log_queue->pop(single_log)) {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        fflush(m_fp);
        m_mutex.unlock();
    }

    printf("async_write_log() is error\n");
}


// 6. 初始化
bool Log::init(const char* file_name, int log_buf_size, int max_lines, int max_queue_size) {
    // 6.1 如果 max_queue_size >= 1，则为异步写
    if (max_queue_size >= 1) {
        m_is_async = true;
        m_log_queue = new Block_Queue<string>[max_queue_size];

        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    else {
        m_is_async = false;
        m_log_queue = NULL;
    }


    // 6.2 初始化 m_buf_size，m_buf，m_max_line
    m_buf_size = log_buf_size;
    m_buf = new char[m_buf_size];
    memset(m_buf, 0, m_buf_size);

    m_max_line = max_lines;


    // 6.3 获取系统的当前时间, 并初始化 m_today
    time_t t = time(NULL);
    struct tm* sys_time = localtime(&t);
    m_today = sys_time->tm_mday;


    // 6.4 初始化 m_dir_name, m_log_name
    const char* p = strrchr(file_name, '/');
    char log_full_name[1024] = {0};

    if (p == NULL) {
        snprintf(log_full_name, 1023, "%d_%02d_%02d_%s", sys_time->tm_year + 1900, 
        sys_time->tm_mon + 1, sys_time->tm_mday, file_name);

        strcpy(m_log_name, file_name);
    }
    else {
        strcpy(m_log_name, p + 1);
        strncpy(m_dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 1023, "%s%d_%02d_%02d_%s", m_dir_name, sys_time->tm_year + 1900, 
        sys_time->tm_mon + 1, sys_time->tm_mday, m_log_name);
    }


    // 6.5 初始化 m_fp
    m_fp = fopen(log_full_name, "a");

    if (m_fp == NULL) return false;
    return true;
}


// 7. 写日志
void Log::write_log(int level, const char* format, ...) {
    // 7.1 获取当前时间
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);

    time_t t = tv.tv_sec;
    struct tm* now_time = localtime(&t);

    // 7.2
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 7.3 如果天数不同，或者 m_line_count >= m_max_line则重新打开一个文件
    m_mutex.lock();
    ++m_line_count;

    if (m_today != now_time->tm_mday || m_line_count >= m_max_line) {
        fflush(m_fp);
        fclose(m_fp);

        char new_log_name[1024] = {0};
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", now_time->tm_year + 1900, now_time->tm_mon + 1, now_time->tm_mday);

        if (m_today != now_time->tm_mday) {
            snprintf(new_log_name, 1023, "%s%s", tail, m_log_name);
            m_today = now_time->tm_mday;
            m_line_count = 0;
        }
        else {
            snprintf(new_log_name, 1023, "%s%s_%d", tail, m_log_name, m_line_count / m_max_line);
        }

        m_fp = fopen(new_log_name, "a");
    }

    m_mutex.unlock();


    // 7.4 确定日志
    va_list va;
    va_start(va, format);

    string log_str;
    m_mutex.lock();

    int n = snprintf(m_buf, m_buf_size - 1, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     now_time->tm_year + 1900, now_time->tm_mon + 1, now_time->tm_mday,
                     now_time->tm_hour, now_time->tm_min, now_time->tm_sec, tv.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_buf_size - n - 1, format, va);

    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    printf("log_%02d: %s", m_line_count ,m_buf);

    va_end(va);
    m_mutex.unlock();


    // 7.5  写入日志
    if (m_is_async && !m_log_queue->full()) {
        m_log_queue->push(log_str);
    }
    else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        fflush(m_fp);
        m_mutex.unlock();
    }
}

