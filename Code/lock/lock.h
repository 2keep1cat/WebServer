#ifndef LOCK
#define LOCK

#include <exception>
#include <semaphore.h>
#include <pthread.h>
//#include <stdio.h>
class sem{//信号量
public:
    //定义两个构造函数用于初始化信号量，一个没有参数（信号量默认为0），一个参数为int num用于指定初始化的信号量的初始值
    sem(){
        if (sem_init(&m_sem, 0, 0) != 0){
            //perror("init err");//c语言标准库<stdio.h>中的函数
            throw std::exception();
        }
    }
    sem(int num){
        if (sem_init(&m_sem, 0, num) != 0){
            throw std::exception();
        }
    }
    //定义一个析构函数用于销毁信号量
    ~sem(){
        sem_destroy(&m_sem);
    }
    //封装申请资源和释放资源的函数并在成功时返回1失败时返回0，sem_wait()为wait(),sem_post()为post()
    bool wait(){
        return sem_wait(&m_sem)==0;
    }
    bool post(){
        return sem_post(&m_sem)==0;
    }
private:
    //定义一个信号量变量
    sem_t m_sem;
};
class locker{//互斥锁
public:
//构造函数，初始化互斥锁
    locker(){
        if(pthread_mutex_init(&m_mutex,NULL)!=0){//设置为NULL使用默认属性
            throw std::exception();
        }
    }
//析构函数，销毁互斥锁
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }
//将pthread_mutex_lock()封装为lock函数，成功返回1，失败返回0
    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }
//将pthread_mutex_unlock()封装为unlock函数，成功返回1，失败返回0
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }
//返回互斥锁的指针类型函数 *get()
    pthread_mutex_t *get(){
        return &m_mutex;
    }
private:
//定义一个mutex类型的互斥锁变量
    pthread_mutex_t m_mutex;
};
class cond{//条件变量
public:
//构造函数和析构函数
    cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
//将pthread_cond_wait函数封装为wait()函数，成功返回1，失败返回0
    bool wait(pthread_mutex_t *mutex){
		int ret = 0;
        ret = pthread_cond_wait(&m_cond,mutex);
        return ret == 0;
    }
//将pthread_cond_timedwait函数封装为timewait()函数，成功返回1，失败返回0
    bool timewait(pthread_mutex_t *mutex, struct timespec t){
        int ret = 0; 
		ret = pthread_cond_timedwait(&m_cond,mutex,&t);
        return ret == 0;
    }
//将pthread_cond_signal函数封装为signal()函数，成功返回1，失败返回0
    bool signal(){
        return pthread_cond_signal(&m_cond)==0;
    }
//将pthread_cond_broadcast函数封装为broadcast()函数，成功返回1，失败返回0
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
//定义条件变量
    pthread_cond_t m_cond;
};

#endif
