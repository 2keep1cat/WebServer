#ifndef THREAD_SYNC
#define THREAD_SYNC

#include <iostream>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
//#include <exception>
extern pthread_cond_t m_cond;//全局变量在头文件对应的cpp文件中定义并初始化，在头文件中声明是全局变量
extern pthread_mutex_t m_mutex;
class sem{
public:
    sem();
    ~sem();
private:
    sem_t m_sem;
};
bool wait();
bool signal();

#endif
