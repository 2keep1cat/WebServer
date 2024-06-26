# 五种I/O模型
1. 阻塞IO:调用者调用了某个函数，等待这个函数返回，期间什么也不做，不停的去检查这个函数有没有返回，必须等这个函数返回才能进行下一步动作

2. 非阻塞IO:非阻塞等待，每隔一段时间就去检测IO事件是否就绪。没有就绪就可以做其他事。非阻塞I/O执行系统调用总是立即返回，不管时间是否已经发生，若时间没有发生，则返回-1，此时可以根据errno区分这两种情况，对于accept，recv和send，事件未发生时，errno通常被设置成eagain

3. 信号驱动IO:linux用套接口进行信号驱动IO，安装一个信号处理函数，进程继续运行并不阻塞，当IO时间就绪，进程收到SIGIO信号。然后处理IO事件。

4. IO复用:linux用select/poll函数实现IO复用模型，这两个函数也会使进程阻塞，但是和阻塞IO所不同的是这两个函数可以同时阻塞多个IO操作。而且可以同时对多个读操作、写操作的IO函数进行检测。知道有数据可读或可写时，才真正调用IO操作函数

5. [异步IO](./appendix/同步与异步.md):linux中，可以调用aio_read函数告诉内核描述字缓冲区指针和缓冲区的大小、文件偏移及通知的方式，然后立即返回，当内核将数据拷贝到缓冲区后，再通知应用程序

`注意：阻塞I/O，非阻塞I/O，信号驱动I/O和I/O复用都是同步I/O。同步I/O指内核向应用程序通知的是就绪事件，比如只通知有客户端连接，要求用户代码自行执行I/O操作，异步I/O是指内核向应用程序通知的是完成事件，比如读取客户端的数据后才通知应用程序，由内核完成I/O操作。`

# 同步I/O模型的工作流程如下（epoll_wait为例）
1. `主线程`往`epoll内核事件表`上注册socket的`读就绪事件`

2. 主线程调用`epoll_wait`等待socket上有数据可读

3. 当socket上有数据可读，epoll_wait通知主线程,主线程循环从socket读取数据，直到没有更多数据可读，然后将读取到的数据封装成一个`请求对象`并插入`请求队列`。

4. 睡眠在请求队列上某个`工作线程`被唤醒，它获得请求对象并处理客户请求得到待输出的结果，然后往epoll内核事件表中注册该socket上的写就绪事件

5. 主线程调用epoll_wait等待socket可写

6. 当socket上有数据可写，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果

<p align="center">
<img src="img/3.png" style="zoom:40%"/>
</p>

上图为个人理解，不一定准确，原文件见`img/1.pptx`

[-->下一篇](./并发编程模式.md)