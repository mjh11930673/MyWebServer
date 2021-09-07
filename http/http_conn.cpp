#include "http_conn.h"



// 1. 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The request file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 2. 网站的根目录，及你的root文件夹的目录
const char *doc_root = "/home/mjh/github/TinyWebServer/root";

// 3. 存储数据库中的用户名和密码
static map<string, string> users;
static Mutex m_lock;
static const bool is_et = true;     // 是否设置为et，与 main.cpp 下的 is_et 一起改，如果需要改的话



// 4. 将 fd 设置为 非阻塞
int setnonblocking(int fd) {
    int old_flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);

    return old_flags;
}


// 5. epollfd 监视 fd
void addfd(int epollfd, int fd, bool is_oneshot) {
    struct epoll_event ev;
    ev.data.fd = fd;

    if (is_et) ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else ev.events = EPOLLIN | EPOLLRDHUP;

    if (is_oneshot) ev.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);
}


// 6. epollfd 清除 fd
void delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}


// 7. epollfd 修改 fd, 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int event) {
    struct epoll_event ev;
    ev.data.fd = fd;
    
    if (is_et) ev.events = event | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    else ev.events = event | EPOLLRDHUP | EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}


// 8. 静态变量的初始化
int HTTP_Conn::m_epollfd = -1;
int HTTP_Conn::m_user_count = 0;


// 9. 将数据库中的所有用户名和密码取出，放入上面的users中
void HTTP_Conn::init_mysql_result(Connection_Pool* connpool) {
    // 5.1 初始化mysql连接
    MYSQL* mysql;
    ConnectionRAII(&mysql, connpool);

    // 5.2 查询
    if (mysql_query(mysql, "select username, passwd from user") != 0) {
        LOG_ERROR("mysql_query() is error: %s", mysql_error(mysql));
    }

    // 5.3 获取查询结果
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == NULL) LOG_ERROR("mysql_store_result() is error: %s", mysql_error(mysql));

    // 5.4 从结果集中获取每一行，将对应的用户名和密码，存入users中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        users[row[0]] = row[1];
    }

    LOG_INFO("database info: ");
    for (auto info : users) {
        LOG_INFO("username: %s, password: %s", info.first.c_str(), info.second.c_str());
    }
    LOG_INFO(" ");
}


// 10. 关闭连接
void HTTP_Conn::close_conn(bool read_close) {
    if (read_close && (m_sockfd != -1)) {
        delfd(m_epollfd, m_sockfd);
        printf("sockfd: %d close\n", m_sockfd);

        m_sockfd = -1;
        --m_user_count;
    }
}


// 11. 初始化连接
void HTTP_Conn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_addr = addr;
    // int reuse = 1;
    //setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    ++m_user_count;

    init();
    LOG_INFO("HTTP_Conn::init() is ok, epollfd: %d, connfd: %d, m_user_count: %d", m_epollfd, m_sockfd, m_user_count);
}


void HTTP_Conn::init() {
    m_mysql = NULL;
    m_read_idx = 0;     
    m_checked_idx = 0;                                      
    m_start_line = 0;                    
  
    m_write_idx = 0;                        

    m_check_state = CHECK_STATE_REQUESTLINE;         
    m_method = GET;            

    m_url = NULL;                          
    m_version = NULL;                        
    m_host = NULL;                          
    m_content_len = 0;                     
    m_linger = false;  

    m_cgi = 0;  
    m_string  = NULL;
    m_bytes_to_send = 0;
    m_bytes_have_send = 0;

    //char* m_file_addr = NULL;                                   
    //int m_iv_count = 0;

    memset(m_read_buf, 0, READ_BUF_SIZE);
    memset(m_write_buf, 0, WRITE_BUF_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);                    
}


// 12. 主线程的读操作
bool HTTP_Conn::read() {
    if (m_read_idx >= READ_BUF_SIZE) return false;

    int read_bytes = 0;

    if (is_et) {
        while (true) {
            read_bytes = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);

            if (read_bytes == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    break;
                }
                LOG_INFO("client connfd: %d recv is error", m_sockfd);
                return false;
            }
            else if (read_bytes == 0) {
                LOG_INFO("client connfd: %d recv is close", m_sockfd);
                return false;
            }

            m_read_idx += read_bytes;
        }
    }
    else {
        read_bytes = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
        m_read_idx += read_bytes;

        if (read_bytes <= 0) {
            LOG_INFO("client connfd: %d recv is close or error", m_sockfd);
            return false;
        }
    }
    
    LOG_INFO("main thread read ok, recv message: ");
    LOG_INFO(m_read_buf);
    return true;
}


// 13. 主线程的写操作
bool HTTP_Conn::write() {
    if (m_write_idx == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    // int bytes_have_send = 0;            // 7.1 已经发送的字节数
    // int bytes_to_send = m_write_idx;    // 7.2 需要发送的字节数
    int write_bytes = 0;                // 7.3 一次发送的字节数

    while (true) {
        write_bytes = writev(m_sockfd, m_iv, m_iv_count);

        if (write_bytes == -1) {
            // 7.4 发送缓冲区已满，继续监视EPOLLOUT，等待下次发送
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }

            unmap();
            LOG_INFO("main thread send error");
            return false;
        }
        
        m_bytes_have_send += write_bytes;
        m_bytes_to_send -= write_bytes;

        if (m_bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_addr + (m_bytes_have_send - m_write_idx);
            m_iv[1].iov_len = m_bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + m_bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - m_bytes_have_send;
        }

        // 7.5 发送完毕
        if (m_bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            LOG_INFO("main thread send ok, send bytes: %d", m_bytes_have_send);

            if (m_linger) {
                init();
                return true;
            }
            else return false;
        }
    }

    return true;
}



// 14. 处理客户请求: 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HTTP_Conn::process() {
    LOG_INFO("process_read() begin");
    HTTP_CODE read_ret = process_read();
    LOG_INFO("process_read() end, read_ret: %d", read_ret);

    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


// 15. 解析HTTP请求，由子线程负责处理 
HTTP_Conn::HTTP_CODE HTTP_Conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE http_ret = NO_REQUEST;
    char* text = NULL;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) 
    {
        text = get_line();
        m_start_line = m_checked_idx;

        LOG_INFO("m_check_state: %d, line_status: %d", m_check_state, line_status);
        LOG_INFO("got 1 http line: %s", text);

        switch (m_check_state) 
        {
            case CHECK_STATE_REQUESTLINE:
            {
                http_ret = parse_request_line(text);
                if (http_ret == BAD_REQUEST) return BAD_REQUEST;

                break;
            }

            case CHECK_STATE_HEADER:
            {
                http_ret = parse_headers(text);
                if (http_ret == BAD_REQUEST) return BAD_REQUEST;
                else if (http_ret == GET_REQUEST) return do_request();

                break;
            }

            case CHECK_STATE_CONTENT:
            {
                http_ret = parse_content(text);
                if (http_ret == GET_REQUEST) {
                    return do_request();
                }

                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
                break;
            }
        }
    }

    return NO_REQUEST;
}


// 16. 填充HTTP应答，由子线程负责处理 
bool HTTP_Conn::process_write(HTTP_CODE ret) {
    bool add_ret = false;

    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            add_ret = add_content(error_500_form);
            if (!add_ret) return false;

            break;
        }

        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            add_ret = add_content(error_400_form);
            if (!add_ret) return false;


            break;
        }

        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            add_ret = add_content(error_404_form);
            if (!add_ret) return false;

            break;
        }

        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            add_ret = add_content(error_403_form);
            if (!add_ret) return false;

            break;
        }

        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);

                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_addr;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;

                m_bytes_to_send = m_write_idx + m_file_stat.st_size;

                return true;
            }
            else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                add_ret = add_content(ok_string);
                if (!add_ret) return false;
            }

            break;
        }

        default:
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;

    return true;
}



// 17. 下面这组函数被process_read()调用，以分析HTTP请求，这部分代码具体参考8.6节
// 17.1 解析出一行内容：回车符('\t') + 换行符('\n') 表示一行
HTTP_Conn::LINE_STATUS HTTP_Conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];

        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
        else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

// 17.2 分析请求行
HTTP_Conn::HTTP_CODE HTTP_Conn::parse_request_line(char* text) {
    // 1. strpbrk(s1, s2): 返回s1中第一个出现在 s2 中的字符指针
    // 1. strspn(s1, s2): 检索字符串 s1 中第一个不在字符串 s2 中出现的字符下标
    // text: "GET / HTTP/1.1"                       // 请求
    // text: "POST /0 HTTP/1.1"                     // 注册
    // text: "POST /3CGISQL.cgi HTTP/1.1"
    m_url = strpbrk(text, " \t");                   // m_url: " / HTTP/1.1"
    if (m_url == NULL) return BAD_REQUEST;
 
    *m_url++ = '\0';                                // text: "GET"
    m_url += strspn(m_url, " \t");                  // m_url: "/ HTTP/1.1"

    // 2. strcasecmp(s1, s2): 比较参数 s1 和 s2 字符串，比较时会自动忽略大小写的差异
    char* method = text;                            // method: "GET"
    if (strcasecmp(method, "GET") == 0) {
        LOG_INFO("m_method == GET");
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0){
        LOG_INFO("m_method == POST");
        m_method = POST;
        m_cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }

    // 3. m_version, m_url: "/ HTTP/1.1"
    m_version = strpbrk(m_url, " \t");              // m_version: " HTTP/1.1"
    if (m_version == NULL) return BAD_REQUEST;

    *m_version++ = '\0';                            // m_url: "/"
    m_version += strspn(m_version, " \t");          // m_version: "HTTP/1.1"

    if (strcasecmp(m_version, "HTTP/1.1") == 0) {
        LOG_INFO("HTTP version == 1.1");
    }
    else {
        return BAD_REQUEST;
    }

    // 4. strchr(): 在一个串中查找给定字符的第一个匹配之处, m_url: "/"
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1) strcat(m_url, "judge.html");    // m_url: "/judge.html"
    LOG_INFO("m_url: %s", m_url);

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 17.3 分析头部字段
HTTP_Conn::HTTP_CODE HTTP_Conn::parse_headers(char* text) {
    // 1. 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        if (m_content_len != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }

    // 2. 处理Connection头部字段, text: "Connection: keep-alive"
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");

        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }

    // 3. 处理Content-Length头部字段
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_len = atoi(text); 
    }

    // 4. 处理Host头部字段, text: "Host: 10.0.0.103:9000"
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");

        char* temp = strpbrk(text, ":");
        *temp = '\0';

        m_host = text;      // m_host: "10.0.0.103"
    }

    // 5. else
    else {
        //LOG_INFO("unknow header: %s", text);
    }

    return NO_REQUEST;
}

// 17.4 分析内容字段: 没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入
HTTP_Conn::HTTP_CODE HTTP_Conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_len + m_checked_idx)) {
        text[m_content_len] = '\0';
        m_string = text;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// 17.5 分析目标文件的属性
HTTP_Conn::HTTP_CODE HTTP_Conn::do_request() {
    // 1. m_real_file = doc_root + m_url
    // doc_root = "/home/mjh/github/TinyWebServer/root", m_url = "/judge.html"
    LOG_INFO("in do_request(), doc_root: %s, m_url: %s", doc_root, m_url);

    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    const char* p = strrchr(m_url, '/');
    LOG_INFO("*(p + 1) == %c, p: %s", *(p + 1), p);

    // 2. 处理cgi
    if (m_cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char* m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        printf("m_url_real: %s, m_real_file: %s\n", m_url_real, m_real_file);
        free(m_url_real);

        //将用户名和密码提取出来
        //m_string: "user=123456&password=123456"
        char name[100], password[100];
        printf("m_string: %s\n", m_string);

        int i = 0;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {

                m_lock.lock();
                int res = mysql_query(m_mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                {
                    strcpy(m_url, "/log.html");
                    LOG_INFO("register ok");
                }
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 3. 注册
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 4. 登入
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 5. 看图
    else if (*(p + 1) == '5')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 6. 看视频
    else if (*(p + 1) == '6')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 7. 关注
    else if (*(p + 1) == '7')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 8. 
    else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }
    LOG_INFO("m_real_file: %s", m_real_file);


    // 9. 目标文件是否存在, 当前用户是否有读取目标文件的权限, 目标文件是一个目录
    if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

    // 10. 如果目标文件存在、对该用户有权限、且不是目录，则使用mmap将该文件映射到内存地址m_file_addr处
    int fd = open(m_real_file, O_RDONLY);
    m_file_addr = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}



// 18. 下面这组函数被process_write()调用，以填充HTTP应答
void HTTP_Conn::unmap() {
    if (m_file_addr) {
        munmap(m_file_addr, m_file_stat.st_size);
        m_file_addr = NULL;
    }
}

bool HTTP_Conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUF_SIZE) {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUF_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUF_SIZE - 1 - m_write_idx)) return false;

    m_write_idx += len;
    va_end(arg_list);

    return true;
}

bool HTTP_Conn::add_content_len(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool HTTP_Conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HTTP_Conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool HTTP_Conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HTTP_Conn::add_headers(int content_len) {
    add_content_len(content_len);
    add_linger();
    add_blank_line();

    return true;
}

bool HTTP_Conn::add_content(const char* content) {
    return add_response("%s", content);
}







