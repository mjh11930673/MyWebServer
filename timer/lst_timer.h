// 双向链表定时器的实现

#ifndef LST_TIMER
#define LST_TIMER

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include "../log/log.h"


class Util_Timer;

// 1. 用户数据结构
struct Client_Data {
    int sockfd;
    struct sockaddr_in addr;
    Util_Timer* timer;
};


// 2. 定时器类
class Util_Timer {
public:
    Util_Timer() : expire_time(0), cb_func(NULL), user_data(NULL), prev(NULL), next(NULL)
    { }

public:
    time_t expire_time;                 // 任务的超时时间, 绝对时间
    void (*cb_func)(Client_Data*);      // 任务的回调函数
    Client_Data* user_data;             // 用户数据
    Util_Timer* prev;                   // 先前的定时器
    Util_Timer* next;                   // 下一个定时器
};


// 3. 定时器链表：它是一个升序、双向链表，且带有头尾节点
class Sort_List_Timer {
public:
    Util_Timer* head;
    Util_Timer* tail;

public:
    // 1. 构造和析构函数
    Sort_List_Timer() : head(NULL), tail(NULL) { }
    ~Sort_List_Timer();

    // 2. 增：将定时器添加到定时器链表中
    void add_timer(Util_Timer* timer);

    // 3. 删：将定时器从定时器链表中删除
    void del_timer(Util_Timer* timer);

    // 4. 改：调整定时器在定时器链表中的位置，并且只考虑定时器时间延长的情况
    void adjust_timer(Util_Timer* timer);

    // 5. tick函数: SIGALRM信号每次被触发，就在信号处理函数（主函数）中执行一次tick函数
    void tick();

private:
    // 6. 重载的辅助函数，用在 add_timer 和 adjust_timer 函数
    void add_timer(Util_Timer* timer, Util_Timer* lst_timer);
};


#endif