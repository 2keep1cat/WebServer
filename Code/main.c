#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

#define SYNLOG  //同步写日志
//#define ASYNLOG //异步写日志

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

//设置定时器相关参数
static int pipefd[2];//用于表示管道
static sort_timer_lst timer_lst;
static int epollfd = 0;

/*信号处理函数（传入sigaction结构体使用），仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响*/
void sig_handler(int sig)
{
    int save_errno = errno;//--------------------为保证函数的可重入性，保留原来的errno，可重入性表示中断后再次进入该函数，环境变量与之前相同
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);//-------将信号值从管道写端写入，传输字符类型，而非整型
    errno = save_errno;//------------------------将原来的errno赋值为当前的errno
}

/*通过sigaction结构体对信号设置处理方式*/
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
                                            //---即handler信号处理函数、SA_RESTART和屏蔽信号处理函数执行期间屏蔽所有信号
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);//alarm 用于设置一个定时器，在TIMESLOT秒后发送 SIGALRM 信号给进程
}

/*定时器回调函数，删除非活动连接在socket上的注册事件，并关闭*/
void cb_func(client_data *user_data)
{                                              //从epollfd实例中删除文件描述符user_data->sockfd对应的事件,0是无用参数
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);//-------------------------确保user_data非空
    close(user_data->sockfd);//------------------关闭文件描述符
    http_conn::m_user_count--;//-----------------对 http_conn 类的静态成员变量 m_user_count 进行自减操作，即用户数-1
    LOG_INFO("close fd %d", user_data->sockfd);//记录关闭文件描述符的操作，以及被关闭的文件描述符的值
    Log::get_instance()->flush();//--------------确保在关闭文件描述符后将日志写入磁盘或输出设备，以避免日志丢失或延迟显示
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型
#endif

    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);

    addsig(SIGPIPE, SIG_IGN);

    //创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "zhuwenjie", "421536", "yourdb", 3306, 8);

    //创建线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }

    http_conn *users = new http_conn[MAX_FD];
    assert(users);

    //初始化数据库读取表
    users->initmysql_result(connPool);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    //struct linger tmp={1,0};
    //SO_LINGER若有数据待发送，延迟关闭
    //setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    //创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    //创建管道套接字，这个算管道吗？
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);//创建一个 UNIX 域套接字对，pipefd 包含两个文件描述符，pipefd[0] 和 pipefd[1]，分别表示读端和写端
    assert(ret != -1);//--------------------------------若返回值为-1就终止执行
    setnonblocking(pipefd[1]);//------------------------将管道的写端设置为非阻塞，以便在写入数据时不会因缓冲区满而阻塞，这样确保信号处理函数可以快速返回。
    addfd(epollfd, pipefd[0], false);//-----------------向内核事件表注册管道的读事件，设置为ET（边缘触发）模式和非阻塞模式

    addsig(SIGALRM, sig_handler, false);//--------------设置SIGALRM的信号处理函数为sig_handler，不设置SA_RESTART
    addsig(SIGTERM, sig_handler, false);//--------------设置SIGTERM的信号处理函数为sig_handler，不设置SA_RESTART
    bool stop_server = false;//-------------------------初始化服务器停止标志

    client_data *users_timer = new client_data[MAX_FD];

    bool timeout = false;//-----------------------------初始化超时标志
    alarm(TIMESLOT);//----------------------------------每隔TIMESLOT时间触发SIGALRM信号

    while (!stop_server)
    {   /*通过 epoll_wait 等待事件的发生。每当有事件发生时，循环处理这些事件*/
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)//-------------监测发生事件的文件描述符
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)//--------------轮询文件描述符
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);

                //初始化client_data数据
                //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
#endif

#ifdef listenfdET
                while (1)
                {
                    int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    //初始化client_data数据
                    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }

            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }

            /*处理信号，如果是管道读端有数据可读（即 EPOLLIN 事件），则从管道读端读取信号值。*/
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                                                                   //从管道读端读出信号值，成功返回字节数，失败返回-1
                                                                   //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv(pipefd[0], signals, sizeof(signals), 0);//读取到的信号值存储在 signals 数组中
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)//------------------遍历读取到的信号值，根据信号类型执行相应的处理逻辑
                    {
                        switch (signals[i])
                        {
                        case SIGALRM://------------------------------如果是SIGALRM，设置 timeout 标志为 true
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM://------------------------------如果是SIGTERM，设置 stop_server 标志为 true
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }

            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once())
                {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    //若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
