#include "thread_sync.h"
pthread_cond_t m_cond = PTHREAD_COND_INITIALIZER; //定义并初始化一个条件变量
pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;//定义并初始化互斥锁
sem::sem(){
        if(sem_init(&(this->m_sem),0,0)!=0){//第二个参数指示了信号量要不要在进程之间共享，
        //为0代表信号量在同一个进程的线程之间共享，不为0代表在不同进程之间的线程间共享
        //第三个参数代表信号量的初始值，此处表示信号量初始值为0
        //成功则返回0，失败返回-1并设置errno
            //throw std::exception();//
            perror("init err");
        }
    }
sem::~sem(){
        sem_destroy(&(this->m_sem));
    }

bool wait(){
    pthread_mutex_lock(&m_mutex);
    int ret = pthread_cond_wait(&m_cond,&m_mutex);//进入函数后，自动解锁，使得其它线程可以访问共享资源，
    //在条件满足后获得互斥锁
    pthread_mutex_unlock(&m_mutex);
    return ret==0;
}
bool signal(){
    return pthread_cond_signal(&m_cond)==0;
}