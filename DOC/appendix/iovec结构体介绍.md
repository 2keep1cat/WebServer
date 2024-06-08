# struct iovec概述
```c
#include <sys/uio.h>
```
## 作用
用于表示`分散/聚集（Scatter/Gather）`I/O 操作，分散是指在读操作时将读取的数据写入多个buffer中，聚集是指在写操作时将多个buffer的数据写入同一个Channel

## 组成
通常由以下两个成员组成：
```
struct iovec {
    ptr_t iov_base; /* Starting address */
    size_t iov_len; /* Length in bytes */
};
```
* ptr_t iov_base：指向缓冲区的起始位置的指针。

指向一个缓冲区，这个缓冲区存放的是readv所接收的数据或是writev将要发送的数据。

* size_t iov_len：缓冲区的大小（以字节为单位）

确定接收的最大长度或者实际写入的长度。

## 用法
通常，这个结构用作一个多元素的数组。

利用readv和writev函数在一次函数调用中读、写多个非连续缓冲区。有时也将这两个函数称为散布读（scatter read）和聚集写（gather write）。

```c
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
 
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```
这两个函数需要三个参数：

* 要在其上进行读或是写的文件描述符fd
* 读或写所用的I/O向量(vector)
* 要使用的向量元素个数(count)

这些函数的返回值是readv所读取的字节数或是writev所写入的字节数。如果有错误发生，就会返回-1，而errno存有错误代码。注意，也其他I/O函数类似，可以返回错误码EINTR来表明他被一个信号所中断。
