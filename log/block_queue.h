// 使用循环数组实现队列

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H


#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;


template<typename T>
class Block_Queue {
private:
    // 1. 成员变量
    T* m_arr;               // 1.1 循环数组
    int m_max_size;         // 1.2 循环数组的最大容量
    int m_size;             // 1.3 循环数组目前的 size
    int m_front;            // 1.4 循环数组的的第一个元素所在的位置（注意并不是0）
    int m_back;             // 1.5 循环数组的的最后一个元素所在的位置

    Mutex m_mutex;          // 1.6 互斥锁
    Cond m_cond;            // 1.7 条件变量


public:
    // 2. 构造函数和析构函数
    Block_Queue(int max_size = 1000) : m_max_size(max_size), m_size(0), m_front(0), m_back(0) 
    {
        if (max_size <= 0) exit(-1);
        m_arr = new T[max_size];
    }
    ~Block_Queue();

    // 3. clear
    void clear();

    // 4. 判断该循环数组，是否 满/空
    bool full();
    bool empty();

    // 5. 返回该循环数组的 第一个元素/最后一个元素
    bool front(T& value);
    bool back(T& value);

    // 6. 返回 m_size / m_max_size
    int size();
    int max_size();

    // 7. 往循环数组中插入
    bool push(const T& item);

    // 8. 往循环数组中删除，重载的 pop() 增加了超时处理
    bool pop(T& item);
    //bool pop(T& item, int ms_timeout);
};



// 2. 析构函数
template<typename T>
Block_Queue<T>::~Block_Queue() {
    clear();
    m_mutex.lock();
    if (m_arr != NULL) delete[] m_arr;
    m_mutex.unlock();
}


// 3. clear
template<typename T>
void Block_Queue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = 0;
    m_back = 0;
    m_mutex.unlock();
}


// 4. 判断该循环数组，是否 满/空
template<typename T>
bool Block_Queue<T>::full() {
    m_mutex.lock();

    if (m_size >= m_max_size) {
        m_mutex.unlock();
        return true;
    }

    m_mutex.unlock();
    return false;
}

template<typename T>
bool Block_Queue<T>::empty() {
    m_mutex.lock();

    if (m_size == 0) {
        m_mutex.unlock();
        return true;
    }

    m_mutex.unlock();
    return false;
}


// 5. 返回该循环数组的 第一个元素/最后一个元素
template<typename T>
bool Block_Queue<T>::front(T& value) {
    m_mutex.lock();

    if (m_size == 0) {
        m_mutex.unlock();
        return false;
    }

    value = m_arr[m_front];
    m_mutex.unlock();

    return true;
}

template<typename T>
bool Block_Queue<T>::back(T& value) {
    m_mutex.lock();

    if (m_size == 0) {
        m_mutex.unlock();
        return false;
    }

    value = m_arr[m_back];
    m_mutex.unlock();

    return true;
}

// 6. 返回 m_size / m_max_size
template<typename T>
int Block_Queue<T>::size() {
    int size = 0;

    m_mutex.lock();
    size = m_size;
    m_mutex.unlock();

    return size;
}

template<typename T>
int Block_Queue<T>::max_size() {
    int max_size = 0;

    m_mutex.lock();
    max_size = m_max_size;
    m_mutex.unlock();

    return max_size;
}


// 7. 往循环数组中插入
template<typename T>
bool Block_Queue<T>::push(const T& item) {
    m_mutex.lock();

    if (m_size >= m_max_size) {
        m_cond.broadcast_cond();
        m_mutex.unlock();
        return false;
    }

    m_arr[m_back] = item;
    m_back = (m_back + 1) % m_max_size;
    ++m_size;

    m_cond.broadcast_cond();
    //m_cond.signal_cond();
    m_mutex.unlock();

    //printf("signal_cond(), push is ok, buf: %s, m_size = %d, m_front = %d, m_back = %d\n\n", item.c_str(), m_size, m_front, m_back);
    return true;
}


// 8. 往循环数组中删除，重载的 pop() 增加了超时处理
template<typename T>
bool Block_Queue<T>::pop(T& item) {
    m_mutex.lock();

    //printf("i am in pop(), now m_size = %d\n", m_size);
    while (m_size <= 0) {
        //printf("i am in pop(), m_size = %d\n", m_size);
        if (!m_cond.wait_cond(m_mutex.get())) {
            printf("m_cond.wait_cond is error\n");
            m_mutex.unlock();
            return false;
        }
        else {
            //printf("i am recv m_cond.broadcast_cond(), m_size = %d\n", m_size);
            //break;
        }
    }

    item = m_arr[m_front];
    m_front = (m_front + 1) % m_max_size;
    --m_size;

    m_mutex.unlock();
    //printf("pop is ok, now m_size = %d, m_front = %d, m_back = %d, buf: %s\n", m_size, m_front, m_back, item.c_str());
    return true;
}

// template<typename T>
// bool Block_Queue<T>::pop(T& item, int ms_timeout) {

// }



#endif