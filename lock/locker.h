#ifndef LOCKER_H
#define LOCKER_H

// 将线程的3种同步机制包装成类
#include <exception>
#include <pthread.h>
#include <semaphore.h>


// 1. 封装信号量的class
class Sem {
private:
    sem_t m_sem;

public:
    // 1.1 构造函数：创建并初始化信号量
    Sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }

    Sem(int value) {
        if (sem_init(&m_sem, 0, value) != 0) {
            throw std::exception();
        }
    }

    // 1.2 析构函数：销毁信号量
    ~Sem() {
        sem_destroy(&m_sem);
    }

    // 1.3 等待信号量
    bool wait_sem() {
        return sem_wait(&m_sem) == 0;
    }

    // 1.4 增加信号量
    bool post_sem() {
        return sem_post(&m_sem) == 0;
    }
};


// 2. 封装互斥锁的class
class Mutex {
private:
    pthread_mutex_t m_mutex;

public:
    // 2.1 构造函数
    Mutex() {
        if (pthread_mutex_init(&m_mutex, NULL)) {
            throw std::exception();
        }
    }

    // 2.2 析构函数
    ~Mutex() {
        pthread_mutex_destroy(&m_mutex);
    }

    // 2.3 锁住互斥锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }


    // 2.4 释放互斥锁 
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }


    // 2.5 获取该锁
    pthread_mutex_t* get() {
        return &m_mutex;
    }
};


// 3. 封装条件变量的class
class Cond {
private:
    //pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;

public:
    // 3.1 构造函数
    Cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    // 3.2 析构函数
    ~Cond() {
        //pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    // 3.3 等待条件变量
    bool wait_cond(pthread_mutex_t* mtex) {
        return pthread_cond_wait(&m_cond, mtex) == 0;
    }

    // 3.4 唤醒等待的条件变量
    bool signal_cond() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 3.5 唤醒所有等待的条件变量
    bool broadcast_cond()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
};



#endif