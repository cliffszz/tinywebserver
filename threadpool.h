#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

// 线程池类，模板参数T为任务类型
template <typename T>
class ThreadPool {
public:
    // 构造函数，默认线程数为8，请求队列最大值为10000
    ThreadPool(int _threadNums = 8, int _maxReqNums = 10000);

    // 析构函数
    ~ThreadPool();

    // 向队列中添加请求，并返回是否添加成功
    bool appendRequest(T* req);

private:
    // 创建线程用的回调函数
    static void* worker(void *arg);

    // 工作线程运行的函数，不断从请求队列中取出任务并执行
    void run();

private:
    // 线程数
    int threadNums;

    // 线程数组
    pthread_t* threads;

    // 请求队列最大请求数
    int maxReqNums;

    // 请求队列
    std::list<T*> reqQueue;

    // 请求队列的互斥锁
    Locker queueLocker;

    // 是否有任务需要处理
    Sem queueStat;

    // 是否结束线程池中的线程
    bool stopThread;
};

// 构造函数
template <typename T>
ThreadPool<T>::ThreadPool(int _threadNums, int _maxReqNUms) :
    threadNums(_threadNums), maxReqNums(_maxReqNUms),
    stopThread(false), threads(nullptr)
{
    // 数据有误，抛出异常
    if (threadNums <= 0 || maxReqNums <= 0) {
        throw std::exception();
    }

    // 初始化线程数组
    threads = new pthread_t[threadNums];
    if (!threads) {
        throw std::exception();
    }

    // 创建threadNums个线程，并设置为线程分离
    // 线程分离：线程结束时，它的资源会被系统自动的回收，而不再需要在其它线程中对其进行pthread_join()操作
    for (int i = 0; i < threadNums; i++) {
        // 创建线程
        printf("Create the %dth thread\n", i);
        if (pthread_create(threads + i, nullptr, worker, this) != 0) {
            delete [] threads;
            throw std::exception();
        }

        // 设置为线程分离
        if (pthread_detach(threads[i]) != 0) {
            delete [] threads;
            throw std::exception();
        }
    }
}

// 析构函数，回收内存空间，停止线程
template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete [] threads;
    stopThread = true;
}

// 向工作队列中添加请求
template<typename T>
bool ThreadPool<T>::appendRequest(T* req) {
    // 对队列加锁
    queueLocker.lock();

    // 添加请求至队列末尾，若超过最大值，解锁,返回添加失败
    if (reqQueue.size() > maxReqNums) {
        queueLocker.unlock();
        return false;
    }
    reqQueue.push_back(req);

    // 对队列解锁
    queueLocker.unlock();

    // 待处理请求加一
    queueStat.post();

    // 返回添加成功
    return true;
}

// 回调函数，执行线程中的任务
template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    // 入参arg表示线程所在的线程池
    ThreadPool* pool = (ThreadPool*)arg;

    // 执行工作函数
    pool->run();

    // 返回线程所在的线程池
    return pool;
}

// 工作函数
template<typename T>
void ThreadPool<T>::run() {
    // 若线程池未停止，循环执行
    while (!stopThread) {
        // 等待是否有待处理的线程
        queueStat.wait();

        // 对请求队列加锁
        queueLocker.lock();

        // 取出队头的请求
        if (reqQueue.empty()) {
            queueLocker.unlock();
            continue;
        }
        T* req = reqQueue.front();
        reqQueue.pop_front();

        // 对请求队列解锁
        queueLocker.unlock();

        // 处理请求
        req->process();
    }
}

#endif