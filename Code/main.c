#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include "./lock/lock.h"
#include "./thread_pool/thread_pool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./mysql/sql_connection_pool.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

#define SYNLOG  //同步写日志
//#define ASYNLOG //异步写日志

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//声明这三个函数，这三个函数在http_conn.cpp中定义
extern int addfd(int epollfd, int fd, bool one_shot);//向内核事件表注册读事件
extern int removefd(int epollfd, int fd);//------------从内核事件表删除文件描述符
extern int setnonblocking(int fd);//-------------------对文件描述符设置非阻塞

//设置定时器相关参数
static int pipefd[2];//--------------------------用于表示管道
static sort_timer_lst timer_lst;//---------------创建定时器容器链表
static int epollfd = 0;

/*通过sigaction结构体对信号设置处理方式，即使用handler作为信号处理函数，默认设置SA_RESTART，信号处理函数执行期间屏蔽所有信号*/
extern void addsig(int sig, void(handler)(int), bool restart);

/*定时处理任务，重新定时以不断触发SIGALRM信号*/
void timer_handler()
{
    timer_lst.tick();//--------------------------先处理定时器容器中的超时任务
    alarm(TIMESLOT);//---------------------------alarm 用于设置定时，在TIMESLOT秒后发送 SIGALRM 信号给当前进程
}

/*信号处理函数（传入sigaction结构体使用），仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响*/
void sig_handler(int sig)
{
    int save_errno = errno;//--------------------为保证函数的可重入性，保留原来的errno，可重入性表示中断后再次进入该函数，环境变量与之前相同
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);//-------将信号值从管道写端写入，传输字符类型，而非整型
    errno = save_errno;//------------------------将原来的errno赋值为当前的errno
}


/*定时器回调函数，删除非活动连接在epoll实例上的注册事件，并关闭*/
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
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型，单例模式
#endif

    int port = 9527;//atoi(argv[1]);//-------------------------将运行程序时输入的端口号转为整型（##可以改为固定端口号以免每次输入）

    addsig(SIGPIPE, SIG_IGN);//-------------------------使用SIG_IGN作为信号处理函数，表示忽略信号SIGPIPE

    //创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "zhuwenjie", "421536", "yourdb", 3306, 8);

    //用自定义的threadpool类创建线程池
    threadpool<http_conn> *pool = NULL;//---------------声明一个指向 threadpool<http_conn> 类型对象的指针 pool，并将其初始化为 NULL
    try //----------------------------------------------使用 try-catch 块来捕获可能的异常情况
    {
        pool = new threadpool<http_conn>(connPool);//---对 threadpool<http_conn> 进行动态分配内存，并将分配的对象的指针赋值给 pool
    }
    catch (...) 
    {
        return 1;//-------------------------------------如果在动态分配内存的过程中出现异常，catch 块将捕获异常，并执行相应的操作
    }

    http_conn *users = new http_conn[MAX_FD];//---------http_conn类数组，用于处理请求报文并生成响应报文
    assert(users);

    //初始化数据库读取表
    users->initmysql_result(connPool);
    //步骤为socket(),setsockopt(),bind(),listen(),epoll_create(),epoll_ctl(),epoll_wait(),accept()
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);//---服务器用于监听客户端的连接请求的套接字
    assert(listenfd >= 0);

    //struct linger tmp={1,0};
    //SO_LINGER若有数据待发送，延迟关闭
    //setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    
    /*----------创建可以用于下面bind()函数绑定的套接字地址------------ */
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));//-----------------将 address 的内存空间清零
    address.sin_family = AF_INET;//---------------------设置地址协议族为IPV4
    address.sin_addr.s_addr = htonl(INADDR_ANY);//------设置IP地址为任意IP地址
    address.sin_port = htons(port);//-------------------设置端口号为运行程序时手动输入的端口号

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));//设置端口复用，可以在套接字关闭后立即重新使用之前绑定的地址
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));//将套接字绑定到指定的地址
    assert(ret >= 0);
    ret = listen(listenfd, 5);//------------------------可以同时等待处理的最大连接数为5
    assert(ret >= 0);

    //创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];//-------------用于存储就绪事件的数组
    epollfd = epoll_create(5);//------------------------epoll实例（内核事件表）的文件描述符
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

    client_data *users_timer = new client_data[MAX_FD];//创建连接资源数组，连接资源包括客户端套接字地址、套接字文件描述符和定时器指针

    bool timeout = false;//-----------------------------初始化超时标志，默认为false
    alarm(TIMESLOT);//----------------------------------隔TIMESLOT秒后触发一次SIGALRM信号

    while (!stop_server)
    {   /*通过 epoll_wait 阻塞等待事件的发生。将就绪事件存入传出参数events中，number为就绪事件数*/
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        /*number < 0 表示 epoll_wait 函数调用出现了错误。具体的错误原因可以通过查看错误码 errno 来判断
        EINTR 表示在等待事件期间收到了一个信号。errno != EINTR，说明发生了其他类型的错误*/
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");//---------输出一个错误日志
            break;
        }

        for (int i = 0; i < number; i++)//--------------轮询文件描述符
        {
            int sockfd = events[i].data.fd;//-----------从events就绪事件数组中取一个事件的文件描述符
            
            if (sockfd == listenfd)//-------------------通过检查 sockfd 是否等于 listenfd，可以确定是否有新的连接请求到达
            {
                struct sockaddr_in client_address;//----新建一个sockaddr_in类型的结构体用于接受客户端的IP地址和端口号
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                //accept从监听套接字listenfd中提取第一个等待连接的请求，并返回一个新的文件描述符 connfd，表示这个新连接的套接字
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
                users[connfd].init(connfd, client_address);//--用提取出的新连接的套接字找到它在users这个http_conn类数组中的位置，并初始化

                //初始化client_data数据
                //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;//将该接收到的连接的套接字的地址放入连接资源数组中
                users_timer[connfd].sockfd = connfd;//---------将该接收到的连接的套接字的文件描述符放入连接资源数组中
                util_timer *timer = new util_timer;//----------新建一个定时器timer和该连接一起放入连接资源数组中，下面给定时器赋值
                timer->user_data = &users_timer[connfd];//-----将该连接的资源赋值给定时器的user_data
                timer->cb_func = cb_func;//--------------------定时器的回调函数设置为cb_func
                time_t cur = time(NULL);//---------------------获取当前时间
                timer->expire = cur + 3 * TIMESLOT;//----------定时器的超时时间设置为当前时间+3倍TIMESLOT(即15秒)
                users_timer[connfd].timer = timer;//-----------设置好该定时器后将定时器加入该连接的连接资源users_timer[connfd]
                timer_lst.add_timer(timer);//------------------将timer定时器加入定时器链表
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
            //events[i].events表示就绪事件数组中的第i个事件的事件类型，
            //EPOLLRDHUP表示对端关闭连接或半关闭连接（即对端关闭了写入），EPOLLHUP表示挂起事件，通常表示连接被挂起或断开，EPOLLERR 表示发生错误事件
            //使用按位与运算符 & 检查当前事件是否包含上述任意一个事件类型
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;//sockfd是第i个事件的文件描述符，这里表示用timer指针指向该事件的连接资源中的定时器
                timer->cb_func(&users_timer[sockfd]);//---------调用定时器中的回调函数删除非活动连接在epoll实例上的注册事件，并关闭

                if (timer)//------------------------------------删除定时器
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

            //当就绪事件数组中的第i个事件的事件类型为可读时，处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;//sockfd是第i个事件的文件描述符，这里表示用timer指针指向该事件的连接资源中的定时器
                if (users[sockfd].read_once())//----------------读取浏览器端发来的请求报文，users是http_conn类数组，如果读取成功
                {                                             //记录日志信息，包括客户端的 IP 地址
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    
                    pool->append(users + sockfd);//-------------pool是线程池，调用append方法向请求队列中插入http_conn类的对象

                    if (timer)//--------------------------------如果定时器 timer 存在，因为有数据传输，所以
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;//---将其到期时间延后 3 个时间单位，并调整定时器在链表中的位置
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else//------------------------------------------如果读取请求报文失败
                {
                    timer->cb_func(&users_timer[sockfd]);//-----调用定时器的回调函数，删除非活动连接在epoll实例上的注册事件
                    if (timer)//--------------------------------如果定时器存在，则从定时器链表 timer_lst 中删除该定时器
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            //当就绪事件数组中的第i个事件的事件类型为可写时
            else if (events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;//在连接资源中获取与当前套接字文件描述符 sockfd 关联的定时器指针
                if (users[sockfd].write())//--------------------尝试向客户端发送数据，如果发送成功
                {                                           //--记录日志，表示数据已发送给客户端
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();//-------------刷新日志缓冲区，将日志写入文件或输出

                    if (timer)//--------------------------------如果有定时器 timer 关联
                    {
                        time_t cur = time(NULL);//--------------获取当前时间 cur
                        timer->expire = cur + 3 * TIMESLOT;//---更新定时器的过期时间 timer->expire，延迟 3 个时间单位 (TIMESLOT)
                        LOG_INFO("%s", "adjust timer once");//--记录日志并刷新
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);//--------调整定时器在定时器列表中的位置。
                    }
                }
                else//------------------------------------------如果发送数据失败
                {
                    timer->cb_func(&users_timer[sockfd]);//-----调用定时器的回调函数，删除非活动连接在epoll实例上的注册事件
                    if (timer)//--------------------------------如果定时器 timer 有效，从定时器列表 timer_lst 中删除该定时器
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout)//--------如果收到了SIGALARM信号，等待上面的读写事件完成后再进行处理，因为处理定时器为非必须事件，收到信号并不是立即处理
        {
            timer_handler();//先处理定时器容器中的超时任务，再设置定时，在TIMESLOT秒后发送 SIGALRM 信号给当前进程
            timeout = false;//重新设置timeout标志为false
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
