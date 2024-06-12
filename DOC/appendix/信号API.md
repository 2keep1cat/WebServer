# sigaction结构体
```c
struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
}
```
sigaction 是一个用于设置和处理信号的结构体，它定义了对信号的处理方式和行为

* sa_handler是一个函数指针，指向信号处理函数

* sa_sigaction同样是信号处理函数，有三个参数，可以获得关于信号更详细的信息

* sa_mask用来指定在信号处理函数执行期间需要被屏蔽的信号

* sa_flags用于指定信号处理的行为

    * SA_RESTART，使被信号打断的系统调用自动重新发起
    ```
    当一个系统调用（如 read()、write()、open() 等）正在进行时，如果接收到一个信号并且没有设置 SA_RESTART 标志，那么系统调用会被中断，返回一个错误码，指示调用被中断。然而，如果设置了 SA_RESTART 标志，则在信号处理函数返回后，被中断的系统调用会自动重启，而不是立即返回错误。这意味着系统调用会在信号处理完成后继续执行，尽可能地保持其原子性和一致性
    ```
    * SA_NOCLDSTOP，使父进程在它的子进程暂停或继续运行时不会收到 SIGCHLD 信号

    * SA_NOCLDWAIT，使父进程在它的子进程退出时不会收到 SIGCHLD 信号，这时子进程如果退出也不会成为僵尸进程

    * SA_NODEFER，使对信号的屏蔽无效，即在信号处理函数执行期间仍能发出这个信号

    * SA_RESETHAND，信号处理之后重新设置为默认的处理方式

    * SA_SIGINFO，使用 sa_sigaction 成员而不是 sa_handler 作为信号处理函数

* sa_restorer一般不使用

# sigaction函数
用于设置和修改信号的处理方式
```c
#include <signal.h>

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
```

* signum表示操作的信号。
以下列举一些常用的信号：
    * SIGALRM：定时器到期
    * SIGTERM：终止信号，通常用于请求进程正常终止
    * SIGHUP：终端挂起或控制进程终止
    * SIGINT：中断信号，通常由用户按下 Ctrl+C 产生
    * SIGQUIT：退出信号，通常由用户按下 Ctrl+\ 产生
    * SIGKILL：强制终止进程信号
    * SIGCHLD：子进程状态改变
    * SIGPIPE：管道破裂，写入关闭的管道或读取没有写端的管道
    # SIGALRM、SIGTERM信号

* act表示对信号设置新的处理方式，该回调函数的参数是固定的，例如：
```c
void handler(int signum);
```

* oldact表示信号原来的处理方式，通过 oldact 参数来获取之前的信号处理方式，以便进行备份或其他操作，将 oldact 参数设置为 NULL，表示不关心之前的处理方式，不进行保存。

返回值，0 表示成功，-1 表示有错误发生。

# sigfillset函数
将参数set信号集初始化，然后把所有的信号加入到此信号集里。
```c
#include <signal.h>

int sigfillset(sigset_t *set);
```
* set 是一个指向 sigset_t 类型的指针，用于表示信号集

```
在实际使用中，可以通过 sigfillset() 函数将所有信号添加到信号集中，然后使用其他信号集操作函数（如 sigaddset()、sigdelset() 等）来进一步操作信号集，例如将特定的信号添加或删除。
```
以下列举一些常用的信号：
* SIGALRM：定时器到期
* SIGTERM：终止信号，通常用于请求进程正常终止
* SIGHUP：终端挂起或控制进程终止
* SIGINT：中断信号，通常由用户按下 Ctrl+C 产生
* SIGQUIT：退出信号，通常由用户按下 Ctrl+\ 产生
* SIGKILL：强制终止进程信号
* SIGCHLD：子进程状态改变
* SIGPIPE：管道破裂，写入关闭的管道或读取没有写端的管道
# SIGALRM、SIGTERM信号
```c
#define SIGALRM  14     //由alarm系统调用产生timer时钟信号
#define SIGTERM  15     //终端发送的终止信号
```
# alarm函数
```c
#include <unistd.h>;

unsigned int alarm(unsigned int seconds);
```
设置信号传送闹钟，即用来设置信号SIGALRM在经过参数seconds秒数后发送给目前的进程。如果未设置信号SIGALRM的处理函数，那么alarm()默认处理终止进程.

# socketpair函数
在linux下，使用socketpair函数能够创建一对套接字进行通信，项目中使用管道通信。
```c
#include <sys/types.h>
#include <sys/socket.h>

int socketpair(int domain, int type, int protocol, int sv[2]);
```

* domain表示协议族，PF_UNIX或者AF_UNIX

* type表示协议，可以是SOCK_STREAM或者SOCK_DGRAM，SOCK_STREAM基于TCP，SOCK_DGRAM基于UDP

* protocol表示类型，只能为0

* sv[2]表示套节字柄对，该两个句柄作用相同，均能进行读写双向操作

返回结果， 0为创建成功，-1为创建失败

# send函数
```c
#include <sys/types.h>
#include <sys/socket.h>

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
```
* sockfd 是一个已连接或未连接的套接字文件描述符，用于标识要发送数据的套接字
* buf 是指向待发送数据的缓冲区的指针
* len 是要发送的数据的大小（以字节为单位）
* flags 是可选的标志参数，用于指定发送操作的行为。常见的标志包括：
    * 0：默认行为，通常表示阻塞发送操作。
    * MSG_DONTWAIT：非阻塞发送操作，即使发送缓冲区已满，也会立即返回。
    * MSG_NOSIGNAL：在发送过程中忽略 SIGPIPE 信号。如果接收方已关闭连接，则不会导致程序终止。
该函数用于将数据从应用程序发送到已连接的套接字。它可以用于发送任何类型的数据，如字符、字节、结构体等

当套接字发送缓冲区变满时，send通常会阻塞，除非套接字设置为非阻塞模式，当缓冲区变满时，返回EAGAIN或者EWOULDBLOCK错误，此时可以调用select函数来监视何时可以发送数据。