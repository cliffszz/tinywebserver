#ifndef LOCKER_H
#define LOCKER_H

#include <bits/types/struct_timespec.h>
#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 互斥锁类
class Locker {
public:
    // 构造函数，初始化失败则抛出异常
    Locker() {
        if (pthread_mutex_init(&mutex, nullptr) != 0) {
            throw std::exception();
        }
    }

    // 析构函数，销毁互斥锁
    ~Locker() {
        pthread_mutex_destroy(&mutex);
    }

    // 加锁，返回是否加锁成功
    bool lock() {
        return pthread_mutex_lock(&mutex) == 0;
    }

    // 解锁，返回是否解锁成功
    bool unlock() {
        return pthread_mutex_unlock(&mutex) == 0;
    }

    // 获取私有的互斥锁地址
    pthread_mutex_t* get() {
        return &mutex;
    }

private:
    pthread_mutex_t mutex;
};

// 条件变量类
class Cond {
public:
    // 构造函数，初始化条件变量失败则抛出异常
    Cond() {
        if (pthread_cond_init(&cond, nullptr) != 0) {
            throw std::exception();
        }
    }

    // 析构函数，销毁条件变量
    ~Cond() {
        pthread_cond_destroy(&cond);
    }

    // 等待条件变量，需要先加锁
    bool wait(pthread_mutex_t* mutex) {
        int ret = 0;
        ret = pthread_cond_wait(&cond, mutex);
        return ret == 0;
    }

    // 等待条件变量一段时间，需要先加锁
    bool timewait(pthread_mutex_t* mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&cond, mutex, &t);
        return ret == 0;
    }

    // 唤醒一个在等待此条件变量的线程
    bool signal() {
        return pthread_cond_signal(&cond) == 0;
    }

    // 唤醒全部在等待此条件变量的线程
    bool broadcast() {
        return pthread_cond_broadcast(&cond) == 0;
    }

private:
    pthread_cond_t cond;
};

// 信号量类
class Sem {
public:
    // 默认构造函数，初始化信号量
    Sem() {
        if (sem_init(&sem, 0, 0) != 0) {
            throw std::exception();
        }
    }

    // 重载构造函数，初始化的信号量为n
    Sem(int n) {
        if (sem_init(&sem, 0, n) != 0) {
            throw std::exception();
        }
    }

    // 析构函数
    ~Sem() {
        sem_destroy(&sem);
    }

    // 增加信号量
    bool post() {
        return sem_post(&sem) == 0;
    }

    // 减少信号量
    bool wait() {
        return sem_wait(&sem) == 0;
    }

private:
    sem_t sem;
};

#endif