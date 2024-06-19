#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <signal.h>
#include "../log/log.h"

/*通过sigaction结构体对信号设置处理方式，即使用handler作为信号处理函数，默认设置SA_RESTART，信号处理函数执行期间屏蔽所有信号*/
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;//-----------------------创建sigaction结构体变量
    memset(&sa, '\0', sizeof(sa));//-------------memset()用于将一块内存区域设置为指定值，&sa指向内存区域的指针，
                                  //-------------'\0'是要设置的值（字符串终止符），sizeof(sa)是要设置的字节数
    sa.sa_handler = handler;//-------------------信号处理函数中仅仅发送信号值，不做对应逻辑处理
    if (restart)
        sa.sa_flags |= SA_RESTART;//-------------通过restart参数选择是否设置SA_RESTART，即被信号打断的系统调用自动重启
    sigfillset(&sa.sa_mask);//-------------------将所有信号添加到信号集sa_mask中，sa_mask用来指定在信号处理函数执行期间需要被屏蔽的信号
                                            /*---assert() 是一个宏定义，用于在程序中进行断言（Assertion）检查，
                                              ---断言是一种用于检查程序中的假设条件是否成立的工具，
                                              ---assert() 宏接受一个表达式作为参数，
                                              ---如果该表达式的值为假，则断言失败并终止程序的执行。
                                              ---如果表达式的值为真，则断言通过，程序继续执行。*/
    assert(sigaction(sig, &sa, NULL) != -1);//---执行sigaction函数，对传入的sig信号设置新的处理方式sa，
                                            //---即handler信号处理函数、SA_RESTART和信号处理函数执行期间屏蔽所有信号
}

class util_timer;//------------------前向声明（只能在指针或引用类型成员时使用），连接资源结构体成员需要用到定时器类

/*定时器类中的连接资源*/
struct client_data
{ 
    sockaddr_in address;//-----------客户端套接字地址
    int sockfd;//--------------------套接字文件描述符
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

    /*定时任务处理函数，处理超时任务*/
    void tick()
    {
        if (!head)//---------------------------如果头节点为空，说明链表为空，结束
        {
            return;
        }
        //printf( "timer tick\n" );
        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);//-------------获取当前时间
        util_timer *tmp = head;//--------------新建定时器指针tmp指向头节点，用于删除节点
        while (tmp)//--------------------------tmp始终指向第一个节点，对所有超时节点进行处理
        {
            if (cur < tmp->expire)//-----------目前节点的超时时间还没到，它后面的也必然都没到，退出循环
            {
                break;
            }
            tmp->cb_func(tmp->user_data);//----超时了，则用定时器的回调函数处理
            head = tmp->next;//----------------处理完后就可以删除该节点
            if (head)//------------------------如果链表空了，则把prev指针置空，之前一直未处理
            {
                head->prev = NULL;
            }
            delete tmp;//----------------------删除该节点对应的内存空间
            tmp = head;//----------------------tmp继续指向头节点
        }
    }

private:
    /*私有成员，被公有成员add_timer和adjust_time调用
    用于添加timer计时器，要求头节点非空且timer的超时时间晚于lst_head*/
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;//---------新建定时器指针prev指向头节点
        util_timer *tmp = prev->next;//--------新建定时器指针tmp指向头节点的下一个
        while (tmp)//--------------------------遍历链表，按照超时时间找位置，插入prev和tmp之间
        {
            if (timer->expire < tmp->expire)//-直到timer的超时时间早于tmp的
            {
                prev->next = timer;//----------就将timer插入到prev和tmp之间
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;//----------------------还没到目标位置就让tmp和prev一起移动
            tmp = tmp->next;
        }
        if (!tmp)//----------------------------遍历完发现，目标定时器需要放到尾结点处
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;//------------------------头结点
    util_timer *tail;//------------------------尾结点
};

#endif
