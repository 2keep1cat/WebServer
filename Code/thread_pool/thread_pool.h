#ifndef THREAD_POOL
#define THREAD_POOL

#include <iostream>
#include <pthread.h>
#include "../mysql/sql_connection_pool.h"
//线程处理函数和运行函数设置为私有属性
template<typename T>
class threadpool{
    public:
        //thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量
        //connPool是数据库连接池指针
        threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
        ~threadpool();

        //向请求队列中插入任务请求
        bool append(T* request);

    private:
        //工作线程运行的函数，它不断从工作队列中取出任务并执行之
        static void *worker(void *arg);

        void run();

    private:
        //定义线程池中的线程数
        int m_thread_number;
        //定义请求队列中允许的最大请求数
        int m_max_requests;
        //定义描述线程池的数组（线程类型指针），其大小为m_thread_number
        pthread_t *m_threads;
        //定义请求队列,类型用T *,std::list是双向链表，不支持随机访问   
        std::list<T *> m_workqueue;
        //定义保护请求队列的互斥锁,类型用lock.h中的互斥锁类   
        locker m_queuelocker;
        //定义信号量类，表示是否有任务需要处理，用lock.h中信号量类
        sem m_queuestat;
        //是否结束线程
        bool m_stop;

        //数据库连接池
        connection_pool *m_connPool;  
};

template<typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number, int max_request):m_connPool(connPool),m_thread_number(thread_number),m_max_requests(max_request),m_threads(NULL),m_stop(false)
{
    //如果线程数小于等于0或者允许的最大请求数小于等于0抛出异常
    if(thread_number <= 0 || max_request <= 0){
        throw std::exception();
    }
    //用new给线程池数组分配内存空间
    m_threads = new pthread_t[m_thread_number];
    //如果线程池数组的指针为空，报错
    if(!m_threads){
        throw std::exception();
    }
    //创建thread_number个线程并设置线程分离，任意线程创建失败都删除整个线程池，并报错
    for(int i=0;i<thread_number;i++){
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])!=0){
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template<typename T>
threadpool<T>::~threadpool(){
    //删除线程池
    delete[] m_threads;
    //把结束线程的标志设为true
    m_stop = true;
}
template<typename T>
bool threadpool<T>::append(T* request){
    //写请求队列之前先上锁，保证对下面的请求队列进行修改的语句不会同时被多个线程执行，目前我的理解是互斥锁不是和具体的变量绑定的而是阻塞在代码语句上等到互斥锁打开了才能往下执行
    m_queuelocker.lock();
    //判断请求队列的大小是否超过了最大值，如果超过了，记得先把互斥锁解开，然后返回错误
    if(m_workqueue.size()>m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    //向请求队列中加入请求，即传入函数的参数
    m_workqueue.push_back(request);
    //解锁互斥锁
    m_queuelocker.unlock();
    //释放m_queuestat的资源，给它加1，表示请求队列的中请求的数量加1，为什么不直接用整数表示要用信号量表示？可能是因为这个标志请求队列请求数的信号量是多个线程可以访问到的共享数据，用信号量来表示可以避免竞争问题
    m_queuestat.post();
    //返回true表示成功加入请求队列
    return true;
}
template<typename T>
void *threadpool<T>::worker(void *arg){
    //前面的构造函数在创建线程时指定了本函数为线程的执行函数，且第四个参数（worker函数的参数）为threadpool对象的this指针，
    //pthread_create函数的第四个参数的类型是void *，this指针被默认转换成void *，因此需要现在将其转回threadpool *型
    //将arg转为threadpool *型，存到threadpool *型的指针中
    threadpool * this_pool = (threadpool *)arg;
    //用该指针调用run函数
    this_pool->run();
    //返回指针
    return this_pool;
}
template<typename T>
void threadpool<T>::run(){
    //当线程未结束时，循环
    while(!m_stop){
        //先申请请求队列中的一个请求
        m_queuestat.wait();
        //修改请求队列之前，先上锁
        m_queuelocker.lock();
        //如果请求队列空了，解锁，然后continue
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        //取出请求队列中的第一个请求，解锁
        T * request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        //如果取出的请求为空，continue
        if(!request){
            continue;
        }
        //公众号文章中的写法
            // //从连接池中取出一个数据库连接
            // request->mysql = m_connPool->GetConnection();
            // //(模板类中的方法,这里是http类)进行处理
            // request->process();
            // //将数据库连接放回连接池
            // m_connPool->ReleaseConnection(request->mysql);
        //源码中的写法
        //先创建一个connectionRAII对象
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        //再调用请求的处理函数，这里请求是http类
        request->process();
    }
}

#endif