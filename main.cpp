#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./connectionpool/mysql_connection_pool.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./threadpool/thread_pool.h"
#include "./timer/lst_timer.h"

#define MAX_FD 65536            //最大文件描述符
#define MAX_EVENT_NUMBER 10000  //最大事件数
#define TIMESLOT 10             //最小超时单位


static const bool is_et = true;                 // 是否设置为et，与 http_conn.cpp 下的 is_et 一起改，如果需要改的话
static const bool is_sync_write_log = true;     // 是否同步写日志


static int sig_pipefd[2];
static int epollfd = 0;
static Sort_List_Timer list_timer;



// 1. 信号处理函数
void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}


// 2. 设置信号函数
void addsig(int sig, void(*handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handler;
    if (restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, NULL) != -1);
}


// 3. 链表定时器回调函数
void timer_handler() {
    list_timer.tick();
    alarm(TIMESLOT);
}


// 4. 定时器回调函数
void cb_func(Client_Data* user_data)
{   
    assert(user_data);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    --HTTP_Conn::m_user_count;
    LOG_INFO("close fd %d", user_data->sockfd);
}


// 5. show_error()
void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


// 6. init_sock
int init_sock(int port) {
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    int ret = bind(listenfd, (struct sockaddr*) &addr, sizeof(addr));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    return listenfd;
}



// 7. main
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port is error\n", argv[0]);
        return 1;
    }


    // 1. 初始化日志文件
    if (is_sync_write_log) {
        Log::get_instance()->init("./log/ServerLog", 2000, 800000, 0);
        LOG_INFO("sync write log: log_file_name: ./log/2021_9_7_ServerLog");
    }
    else {
        Log::get_instance()->init("ServerLog", 2000, 800000, 8);
        LOG_INFO("async write log: log_file_name: ./log/2021_9_7_ServerLog");
    }


    // 2. 初始化连接池
    Connection_Pool* conn_pool = Connection_Pool::getInstance();
    //conn_pool->init("localhost", "root", "pw", "dbname", 3306, 8);
    conn_pool->init("mysql server ip", "登入的用户名", "密码", "数据库名", 3306, 8);
    

    // 3. 初始化线程池
    ThreadPool<HTTP_Conn>* thread_pool = new ThreadPool<HTTP_Conn>(conn_pool);


    // 4. 用户数据, 初始化数据库读取表
    HTTP_Conn* users = new HTTP_Conn[MAX_FD];
    assert(users);
    users->init_mysql_result(conn_pool);


    // 5. 监听套接字
    int listenfd = init_sock(atoi(argv[1]));
    LOG_INFO("socket is ok, listenfd: %d", listenfd);


    // 6. epollfd, I/O复用
    struct epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    LOG_INFO("epoll is ok, epollfd: %d", epollfd);

    addfd(epollfd, listenfd, false);
    HTTP_Conn::m_epollfd = epollfd;


    // 7. 信号
    socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    setnonblocking(sig_pipefd[1]);
    addfd(epollfd, sig_pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    LOG_INFO("signals is ok, sig_pipefd[0]: %d", sig_pipefd[0]);


    // 8. 定时器
    Client_Data* users_timer = new Client_Data[MAX_FD];
    assert(users_timer);
    bool timeout = false;
    //alarm(TIMESLOT);
    LOG_INFO("timers is ok");


    // 9. while
    bool stop_server = false;   
    while (!stop_server) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER , -1);
        if ((num < 0) && (errno != EINTR)) {
            LOG_ERROR("%s", "epoll_wait() is error");
            break;
        }

        LOG_INFO("");
        LOG_INFO("epoll_wait() return, num: %d", num);
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;

            // 9.1 读事件
            if (events[i].events & EPOLLIN) {
                if (sockfd == listenfd) {
                    LOG_INFO("EPOLLIN && sockfd == listenfd, sockfd: %d", sockfd);

                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);

                    if (!is_et) {
                        int connfd = accept(sockfd, (struct sockaddr*) &client_addr, &client_addr_len);
                        if (connfd < 0) {
                            LOG_ERROR("accept() is error");
                            continue;
                        }

                        if (HTTP_Conn::m_user_count > MAX_FD || connfd >= MAX_FD) {
                            show_error(connfd, "Internal server busy");
                            continue;
                        }

                        LOG_INFO("client accept is ok, connfd: %d", connfd);
                        users[connfd].init(connfd, client_addr);

                        // 定时器
                        users_timer[connfd].sockfd = connfd;
                        users_timer[connfd].addr = client_addr;
                        
                        Util_Timer* timer = new Util_Timer();
                        timer->user_data = &users_timer[connfd];
                        timer->cb_func = cb_func;
                        time_t cur = time(NULL);
                        timer->expire_time = cur + 3 * TIMESLOT;

                        users_timer[connfd].timer = timer;

                        list_timer.add_timer(timer);
                    }
                    else {
                        while (1) {
                            int connfd = accept(sockfd, (struct sockaddr*) &client_addr, &client_addr_len);
                            if (connfd < 0) break;

                            if (HTTP_Conn::m_user_count > MAX_FD || connfd >= MAX_FD) {
                                show_error(connfd, "Internal server busy");
                                break;
                            }

                            LOG_INFO("client accept is ok, connfd: %d", connfd);
                            users[connfd].init(connfd, client_addr);

                            // 定时器
                            users_timer[connfd].sockfd = connfd;
                            users_timer[connfd].addr = client_addr;
                            
                            Util_Timer* timer = new Util_Timer();
                            timer->user_data = &users_timer[connfd];
                            timer->cb_func = cb_func;
                            time_t cur = time(NULL);
                            timer->expire_time = cur + 3 * TIMESLOT;

                            users_timer[connfd].timer = timer;

                            list_timer.add_timer(timer);
                        }
                    }
                }
                else if (sockfd == sig_pipefd[0]) {
                    LOG_INFO("EPOLLIN && sockfd == sig_pipefd[0], sockfd: %d", sockfd);

                    char signals[1024];
                    int sig_count = recv(sockfd, signals, sizeof(signals), 0);

                    if (sig_count <= 0) continue;
                    else {
                        for (int j = 0; j < sig_count; ++j) {
                            switch (signals[j])
                            {
                                case SIGALRM:
                                    timeout = true;
                                    break;
                                case SIGTERM:
                                    stop_server = true;
                                    break;
                            }
                        }
                    }
                }
                else {
                    LOG_INFO("EPOLLIN && sockfd == else, sockfd: %d", sockfd);

                    Util_Timer* timer = users_timer[sockfd].timer;
                    // 可以看到，主线程，负责 读与写，当读取完毕后，将该任务添加进线程池的任务队列中，然后唤醒子进程
                    // 然后则由子线程处理，读取的内容 以及 该写入什么内容给客户端
                    if (users[sockfd].read()) {
                        thread_pool->append(users + sockfd);

                        if (timer) {
                            time_t cur = time(NULL);
                            timer->expire_time = cur + 3 * TIMESLOT;

                            list_timer.adjust_timer(timer);
                        }
                    }
                    else {
                        timer->cb_func(&users_timer[sockfd]);
                        list_timer.del_timer(timer);
                    }

                }

            }
            // 9.2 写事件
            else if (events[i].events & EPOLLOUT) {
                LOG_INFO("EPOLLOUT");

                Util_Timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].write()) {
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire_time = cur + 3 * TIMESLOT;

                        list_timer.adjust_timer(timer);
                    }
                }
                else {
                    timer->cb_func(&users_timer[sockfd]);
                    list_timer.del_timer(timer);
                }
            }
            // 9.3 一些错误事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                LOG_INFO("EPOLLRDHUP | EPOLLHUP | EPOLLERR");

                Util_Timer* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                list_timer.del_timer(timer);
            }
            // 9.4 未知事件
            else {
                LOG_INFO("else something happened");
            }
        }

        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }



    close(epollfd);
    close(listenfd);
    close(sig_pipefd[1]);
    close(sig_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete thread_pool;

    return 0;
}