/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/lock.h"
using namespace std;

//阻塞队列
template <class T>
class block_queue //------------------定义一个模板类 block_queue，支持泛型类型 T
{
public:
    block_queue(int max_size = 1000)//构造函数，m_max_size为队列的最大元素个数，默认为 1000
    {
        if (max_size <= 0)//----------如果传入的max_size小于等于0，程序退出
        {
            exit(-1);
        }

        m_max_size = max_size;//------初始化成员变量
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }
    
    void clear()//--------------------清空队列，设置m_size为0，m_front和m_back为-1，并使用互斥锁保护操作
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue()//------------------析构函数，释放分配的数组空间，并使用互斥锁保护操作
    {
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array;

        m_mutex.unlock();
    }
    
    bool full() //--------------------判断队列是否满了，使用互斥锁保护操作
    {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    
    bool empty() //-------------------判断队列是否为空，使用互斥锁保护操作
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    
    bool front(T &value) //-----------获取队首元素，使用互斥锁保护操作
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    
    bool back(T &value) //------------获取队尾元素，使用互斥锁保护操作
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() //---------------------获取队列当前大小，使用互斥锁保护操作
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size()//------------------获取队列最大大小，使用互斥锁保护操作
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }
    
    //若当前没有线程等待条件变量,则唤醒无意义
    //生产者
    bool push(const T &item)//--------添加元素到队列尾部，然后唤醒线程
    {
        m_mutex.lock();
        if (m_size >= m_max_size)//---如果超过队列的数量限制，则不添加，但仍然唤醒线程
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;//-----------添加元素失败
        }
                                //----队列最后一个元素的位置后移一位
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;//-----添加一个元素

        m_size++;//-------------------队列中元素数量加1

        m_cond.broadcast();//---------然后唤醒线程
        m_mutex.unlock();
        return true;//----------------添加元素成功
    }

    //消费者
    bool pop(T &item)//---------------弹出队列首部的元素，如果队列为空则等待条件变量
    {
        m_mutex.lock();
        while (m_size <= 0)//---------如果队列为空，多个消费者的时候，这里要是用while而不是if
        {                   //--------等待条件变量满足，如果函数执行失败则报错，这里的wait函数执行失败返回0
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }
                            //--------队首元素后移1位
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];//----取出原来的队首元素
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理，在项目中没有使用到
    //在超时时间内，如果队列为空，线程会等待直到队列中有元素可用
    bool pop(T &item, int ms_timeout)//-----------------T &item: 用于存储弹出的元素。ms_timeout: 等待超时时间，单位是毫秒
    {
        struct timespec t = {0, 0};//-------------------初始化 timespec 和 timeval 结构体，用于计算超时时间
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);//---------------------获取当前时间
        m_mutex.lock();
        if (m_size <= 0)//------------------------------检查队列是否为空。如果队列为空，计算超时时间，并在条件变量上等待
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;//t.tv_sec: 当前秒数加上超时时间的秒数部分
            t.tv_nsec = (ms_timeout % 1000) * 1000;//---t.tv_nsec: 超时时间的毫秒部分转换为纳秒
            if (!m_cond.timewait(m_mutex.get(), t))//---条件变量等待函数，等待直到队列中有元素被插入或者超时发生，如果返回 false，表示在超时时间内没有元素被插入，线程超时返回
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)//------------------------------如果等待超时且队列仍为空，释放互斥锁并返回 false
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;//---------计算下一个弹出位置m_front
        item = m_array[m_front];//----------------------并从队列中取出元素赋值给item
        m_size--;//-------------------------------------更新队列大小m_size
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    T *m_array;//---------------------队列
    int m_size;//---------------------队列中元素数量
    int m_max_size;//-----------------队列的元素容量
    int m_front;//--------------------第一个元素的前面一位
    int m_back;//---------------------队列最后一个元素的位置
};

#endif
