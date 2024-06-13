#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
//#include "../log/log.h"


class util_timer;//------------------前向声明（只能在指针或引用类型成员时使用），连接资源结构体成员需要用到定时器类

/*定时器设计中的连接资源*/
struct client_data
{
    sockaddr_in address;//-----------客户端套接字地址
    int sockfd;//--------------------文件描述符
    util_timer *timer;//-------------定时器
};

/*定时器类，是一个双向链表，包含4.1定时器设计中的三个要素：连接资源、定时事件（回调函数）、超时时间*/
class util_timer
{
public:             //---------------构造函数，初始化为空指针
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;//-----------------超时时间
    void (*cb_func)(client_data *);//回调函数,cb_func是函数指针（函数指针的名称是可以自定义的），client_data是该函数的参数类型
                                   //使用main.c中的void cb_func(client_data *user_data)函数
    client_data *user_data;//--------连接资源
    util_timer *prev;//--------------前向定时器，指向前一个定时器
    util_timer *next;//--------------后继定时器，指向后一个定时器，实现双向链表结构
};

/*定时器容器类*/
class sort_timer_lst
{
public:

    /*构造函数，初始化头尾节点*/
    sort_timer_lst() : head(NULL), tail(NULL) {}

    /*析构函数，常规销毁链表*/
    ~sort_timer_lst()
    {   
        util_timer *tmp = head;//--------------先定义一个tmp让tmp指向head节点
        while (tmp)//--------------------------当tmp指向的head节点不为空时，循环
        {
            head = tmp->next;//----------------让head指向下一个节点
            delete tmp;//----------------------删除tmp所在的内存空间
            tmp = head;//----------------------继续让tmp指向head节点
        }
    }

    /*添加定时器，内部调用私有成员add_timer */
    void add_timer(util_timer *timer)
    {
        if (!timer)//--------------------------如果timer定时器为空，没有新的定时器需要插入
        {
            return;//--------------------------结束该函数
        }
        if (!head)//---------------------------如果head指针为空
        {
            head = tail = timer;//-------------让head和tail指针都指向timer定时器
            return;//--------------------------结束该函数
        }
        
        
        if (timer->expire < head->expire)//----如果新的定时器超时时间早于head结点
        {
            timer->next = head;//--------------直接将当前定时器结点作为头部结点
            head->prev = timer;
            head = timer;
            return;//--------------------------结束该函数
        }
        add_timer(timer, head);//--------------新定时器不为空，head不为空，新定时器晚于head节点，
                               //--------------则调用私有成员函数add_timer，插入新定时器
    }

    /*任务发生变化时，调整定时器在链表中的位置*/
    void adjust_timer(util_timer *timer)
    {
        if (!timer)//--------------------------如果定时器为空，表示没有定时器需要被调整位置
        {
            return;//--------------------------结束该函数
        }
        util_timer *tmp = timer->next;//-------新建一个定时器指针tmp指向timer定时器的下一位置
                                //-------------如果tmp为空，即被调整的timer定时器在链表尾部
                                //-------------或者timer定时器的超时时刻仍然早于下一个定时器，则不调整
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;//--------------------------结束该函数
        }
        if (timer == head)//-------------------被调整的timer定时器是链表头结点，将定时器取出，重新插入
        {                   //-----------------注意前面timer定时器的超时时间早于下一定时器的情况已经处理完，下面的timer晚于新的head
            head = head->next;//---------------head指针指向下一节点
            head->prev = NULL;//---------------把head节点的前向指针设为NULL
            timer->next = NULL;//--------------把timer定时器的后向指针设为NULL
            add_timer(timer, head);//----------调用私有成员函数add_timer插入新定时器
                                   //----------满足新定时器不为空，head不为空，新定时器晚于head节点
        }
        else//---------------------------------被调整定时器在内部，将定时器取出，重新插入
        {
            timer->prev->next = timer->next;//-取出timer
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);//---调用私有成员函数add_timer插入新定时器
        }
    }

    /*删除定时器*/
    void del_timer(util_timer *timer)
    {
        if (!timer)//--------------------------如果定时器为空，表示没有定时器需要删除
        {
            return;//--------------------------结束该函数
        }
        if ((timer == head)&&(timer == tail))//如果链表中只有该定时器
        {
            delete timer;//--------------------删除该定时器的内存空间
            head = NULL;//---------------------让头尾指针都指向空
            tail = NULL;
            return;//--------------------------退出函数
        }
        if (timer == head)//-------------------如果被删除的定时器为头结点
        {
            head = head->next;//---------------先让head指针指向下一节点
            head->prev = NULL;
            delete timer;//--------------------然后删除timer节点所在内存空间
            return;//--------------------------结束该函数
        }
        if (timer == tail)//-------------------如果被删除的定时器为尾结点
        {
            tail = tail->prev;//---------------让尾指针指向上一节点
            tail->next = NULL;
            delete timer;//--------------------删除timer节点所在内存空间
            return;//--------------------------结束该函数
        }
        timer->prev->next = timer->next;//-----否则被删除的定时器在链表内部，常规链表结点删除
        timer->next->prev = timer->prev;
        delete timer;
    }
    void tick()
    {
        if (!head)
        {
            return;
        }
        //printf( "timer tick\n" );
        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);
        util_timer *tmp = head;
        while (tmp)
        {
            if (cur < tmp->expire)
            {
                break;
            }
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    //私有成员，被公有成员add_timer和adjust_time调用
    //主要用于调整链表内部结点
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        //遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
        while (tmp)
        {
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        //遍历完发现，目标定时器需要放到尾结点处
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    //头尾结点
    util_timer *head;
    util_timer *tail;
};

#endif
