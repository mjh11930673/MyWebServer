#include "lst_timer.h"


// 1. 析构函数
Sort_List_Timer::~Sort_List_Timer() 
{
    Util_Timer* temp = head;
    while (temp)
    {
        head = temp->next;
        delete temp;
        temp = head;
    }
    
}

// 2. 增：将定时器添加到定时器链表中
void Sort_List_Timer::add_timer(Util_Timer* timer) 
{
    if (timer == NULL) {
        LOG_INFO("add timer is failed, timer == NULL");
        return;
    }
    // 1. 定时器链表为 空
    else if (head == NULL) {
        head = timer;
        tail = timer;
        head->prev = NULL;
        tail->next = NULL;
    }
    // 2. 插入头
    else if (timer->expire_time < head->expire_time) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        head->prev = NULL;
    }
    // 3. 插入尾
    else if (timer->expire_time > tail->expire_time) {
        tail->next = timer;
        timer->prev = tail;
        tail = timer;
        tail->next = NULL;
    }
    // 4. 插入中间
    else add_timer(timer, head);

    LOG_INFO("add timer is ok, sockfd: %d", timer->user_data->sockfd);
}

// 3. 删：将定时器从定时器链表中删除
void Sort_List_Timer::del_timer(Util_Timer* timer) 
{
    if (timer == NULL) {
        LOG_INFO("del timer is failed, timer == NULL");
        return;  
    }
    // 1. 定时器链表只有这一个定时器
    else if ((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
    }
    // 2. 删除头定时器
    else if (timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
    }
    // 3. 删除尾定时器
    else if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
    }
    // 4. 删除中间定时器
    else {
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        delete timer;
    }

    LOG_INFO("del timer is ok, sockfd: %d", timer->user_data->sockfd);
}

// 4. 改：调整定时器在定时器链表中的位置，并且只考虑定时器时间延长的情况
void Sort_List_Timer::adjust_timer(Util_Timer* timer) 
{
    if (timer == NULL) {
        LOG_INFO("adjust timer is failed, timer == NULL");
        return;
    }

    Util_Timer* temp = timer->next;
    if ((temp == NULL) || (timer->expire_time < temp->expire_time)) {
        LOG_INFO("not need adjust timer");
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        timer->prev = NULL;
        add_timer(timer, head);
    }
    else {
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        timer->next = NULL;
        timer->prev = NULL;
        add_timer(timer, temp);
    }

    LOG_INFO("adjust timer is ok, sockfd: %d", timer->user_data->sockfd);
}

// 5. tick函数: SIGALRM信号每次被触发，就在信号处理函数（主函数）中执行一次tick函数
void Sort_List_Timer::tick() 
{
    if (head == NULL) return;

    // log
    LOG_INFO("timer tick() once");
    int count = 0;
    Util_Timer* temp = head;
    time_t cur = time(NULL);   // 获得系统的当前时间
    
    while (temp) {
        if (cur < temp->expire_time) break;
        ++count;

        temp->cb_func(temp->user_data);

        head = temp->next;
        if (head) head->prev = NULL;

        delete temp;
        temp = head;
    }

    LOG_INFO("tick del client count: %d", count);
}

// 6. 重载的辅助函数，用在 add_timer 和 adjust_timer 函数
void Sort_List_Timer::add_timer(Util_Timer* timer, Util_Timer* lst_timer) {
    Util_Timer* prev = lst_timer;
    Util_Timer* temp = lst_timer->next;

    while (temp) {
        if (temp->expire_time > timer->expire_time) {
            prev->next = timer;
            timer->next = temp;
            temp->prev = timer;
            timer->prev = prev;
            return;
        }

        prev = temp;
        temp = temp->next;
    }

    if (temp == NULL) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}