#ifndef HTTP_CONN_H
#define HTTP_CONN_H

// 利用线程池实现一个并发的 Web 服务器

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <map>
#include <mysql/mysql.h>
#include <fstream>

#include "../log/log.h"
#include "../lock/locker.h"
#include "../connectionpool/mysql_connection_pool.h"


// 2. 将 fd 设置为 非阻塞
int setnonblocking(int fd);


// 3. epollfd 监视 fd
void addfd(int epollfd, int fd, bool is_oneshot);


// 4. epollfd 清除 fd
void delfd(int epollfd, int fd);


// 5. epollfd 修改 fd, 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int event);


// 5. 
class HTTP_Conn {
public:
    static const int FILENAME_LEN = 200;            // 1. 文件名的最大长度
    static const int READ_BUF_SIZE = 2048;          // 2. 读缓冲区的大小
    static const int WRITE_BUF_SIZE = 1024;         // 3. 写缓冲区的大小

    // 4. HTTP请求的方法
    enum METHOD {
        GET = 0,
        POST = 1,
        HEAD = 2,
        PUT = 3,
        DELETE = 4,
        TRACE = 5,
        OPTIONS = 6,
        CONNECT = 7,
        PATCH = 8
    };

    // 5. 解析客户请求时，主状态机所处的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,        // 5.1 当前正在分析请求行
        CHECK_STATE_HEADER = 1,             // 5.2 当前正在分析头部字段
        CHECK_STATE_CONTENT = 2             // 5.3 当前正在分析内容字段
    };

    // 6. 服务器处理HTTP请求的可能结果
    enum HTTP_CODE {
        NO_REQUEST = 0,                     // 6.1 请求不完整
        GET_REQUEST = 1,                    // 6.2 得到了一个完整的客户请求
        BAD_REQUEST = 2,                    // 6.2 客户请求有语法错误
        NO_RESOURCE = 3,                    // 6.3 没有客户所请求的资源
        FORBIDDEN_REQUEST = 4,              // 6.4 客户对所访问的资源没有权限
        FILE_REQUEST = 5,                   // 6.5 客户请求文件
        INTERNAL_ERROR = 6,                 // 6.6 服务器内部出错
        CLOSED_CONNECTION = 7               // 6.7 表示客户端已经关闭连接了
    };

    // 7. 行的读取状态
    enum LINE_STATUS {
        LINE_OK = 0,                        // 7.1 读取到一个完整的行
        LINE_BAD = 1,                       // 7.2 行出错
        LINE_OPEN = 2                       // 7.3 行数据还不完整
    };


public:
    static int m_epollfd;                   // 8. epollfd
    static int m_user_count;                // 9. 记录用户数量
    MYSQL* m_mysql;                         // 10. 一个mysql连接
    int m_sockfd;                           // 11. 客户的socket

private:
    //int m_sockfd;                         // 11. 客户的socket
    struct sockaddr_in m_addr;              // 12. 客户的addr

    char m_read_buf[READ_BUF_SIZE];         // 13. 读缓冲区
    int m_read_idx;                         // 14. 标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;                      // 15. 当前正在分析的字符在读缓冲区中的位置                       
    int m_start_line;                       // 16. 当前正在解析的行的起始位置

    char m_write_buf[WRITE_BUF_SIZE];       // 17. 写缓冲区
    int m_write_idx;                        // 18. 写缓冲区中待发送的字节数

    enum CHECK_STATE m_check_state;         // 19. 主机当前所处的状态
    enum METHOD m_method;                   // 20. 请求方法

    char m_real_file[FILENAME_LEN];         // 21. 客户请求的目标文件的完整路径，其内容 = doc_root + m_url，doc_root是网站的根目录
    char* m_url;                            // 22. 客户请求的目标文件的文件名
    char* m_version;                        // 23. HTTP的版本号，目前仅支持HTTP_1.1
    char* m_host;                           // 24. 主机名
    int m_content_len;                      // 25. HTTP请求的消息体的长度
    bool m_linger;                          // 26. HTTP请求是否要求保持连接

    char* m_file_addr;                      // 27. 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;                // 28. 目标文件的状态
    struct iovec m_iv[2];                   // 29. 将采用writev来执行写操作
    int m_iv_count;                         // 30. 被写入内存块的数量

    int m_cgi;                              // 31. 是否启用的POST
    char* m_string;                         // 32. 存储请求头数据
    int m_bytes_to_send;                    // 33. 待发送的字节数
    int m_bytes_have_send;                  // 33. 已经发送的字节数


public:
    // 34. 构造函数和析构函数
    HTTP_Conn(){}
    ~HTTP_Conn(){}

public:
    void init(int sockfd, const sockaddr_in& addr);      // 35. 初始化新接收的连接
    void close_conn(bool read_close = true);             // 36. 关闭连接
    void process();                                      // 37. 处理客户请求
    bool read();                                         // 38. 非阻塞读操作
    bool write();                                        // 39. 非阻塞写操作
    sockaddr_in* get_addr() { return &m_addr; }          // 40. 获取地址
    void init_mysql_result(Connection_Pool* connpool);   // 41. 获取 数据库中的用户名和密码

private:
    // 42. 初始化连接
    void init();   

    // 43. 解析HTTP请求                                         
    HTTP_CODE process_read();

    // 44. 填充HTTP应答
    bool process_write(HTTP_CODE ret);

    // 45. 下面这组函数被process_read()调用，以分析HTTP请求，这部分代码具体参考8.6节
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();                           // 45.1 解析出一行内容
    HTTP_CODE parse_request_line(char* text);           // 45.2 分析请求行
    HTTP_CODE parse_headers(char* text);                // 45.3 分析头部字段
    HTTP_CODE parse_content(char* text);                // 45.4 分析内容字段
    HTTP_CODE do_request();                             // 45.5 分析目标文件的属性
    

    // 46. 下面这组函数被process_write()调用，以填充HTTP应答
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_len(int content_len);
    bool add_linger();
    bool add_blank_line();
};



#endif