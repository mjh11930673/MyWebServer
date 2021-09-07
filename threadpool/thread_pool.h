#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../connectionpool/mysql_connection_pool.h"


template<typename T>
class ThreadPool {
private:
    // 1. 成员变量
    int m_thread_num;                   // 1.1 线程池中的线程数
    int m_max_requests;                 // 1.2 请求队列中允许的最大请求数 
    pthread_t* m_threads;               // 1.3 描述线程池的线程数组
    std::list<T*> m_workqueue;         // 1.4 请求队列
    Mutex m_mutex;                      // 1.5 互斥锁
    Sem m_sem;                          // 1.6 信号量
    bool m_stop;                        // 1.7 是否结束线程
    Connection_Pool* m_conn_pool;       // 1.8 连接池

public:
    // 2. 构造函数和析构函数
    ThreadPool(Connection_Pool* conn_pool, int thread_num = 8, int max_requests = 1000);
    ~ThreadPool();

    // 3. 往请求队列中添加任务
    bool append(T* request);

private:
    // 4. 工作线程运行的函数，在C++中，pthread_create的第3个参数必须是静态函数，所以这里用static修饰
    static void* worker(void* arg);

    // 5. run()
    void run();
};



// 2. 构造函数和析构函数
template<typename T>
ThreadPool<T>::ThreadPool(Connection_Pool* conn_pool, int thread_num, int max_requests) 
    : m_thread_num(thread_num), m_max_requests(max_requests), m_threads(NULL), m_stop(false), m_conn_pool(conn_pool)
{
    if ((thread_num <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[thread_num];
    if (m_threads == NULL) {
        throw std::exception();
    }

    // 创建m_thread_num个线程
    int count = 0;
    for (int i = 0; i < m_thread_num; ++i) {
        int ret = pthread_create(m_threads + i, NULL, worker, (void*)this);
        if (ret != 0) {
            LOG_ERROR("create the %dth thread is error", i + 1);
            delete[] m_threads;
            continue;
        }

        ret = pthread_detach(m_threads[i]);
        if (ret != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        ++count;
    }

    LOG_INFO("success create thread num: %d, failed num: %d", count, m_thread_num - count);
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
    m_stop = true;
}


// 3. 往请求队列中添加任务
template<typename T>
bool ThreadPool<T>::append(T* request) {
    // 操作工作队列时一定要加锁，因为它被所有线程共享
    m_mutex.lock();

    if (m_workqueue.size() > m_max_requests) {
        m_mutex.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_mutex.unlock();
    m_sem.post_sem();
    
    LOG_INFO("thread_pool append request is ok, connfd: %d", request->m_sockfd);
    return true;
}


// 4. 工作线程运行的函数，在C++中，pthread_create的第3个参数必须是静态函数，所以这里用static修饰
// 4. 并且想要在class的静态函数中调用普通的成员函数，需要一些方法，具体看书 p304 页
template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

// 5. run()
template<typename T>
void ThreadPool<T>::run() {
    while (!m_stop) {
        m_sem.wait_sem();
        m_mutex.lock();

        if (m_workqueue.empty()) {
            m_mutex.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_mutex.unlock();

        if (request) {
            ConnectionRAII mysqlConn(&request->m_mysql, m_conn_pool); // 初始化：request->mysql，并且会自动释放该连接池
            request->process();
        }
    }
}


#endif