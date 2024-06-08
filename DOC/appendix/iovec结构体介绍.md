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
    void* iov_base; /* starting address of buffer */
    size_t iov_len; /* size of buffer */
};
```
* void* iov_base：指向缓冲区的起始位置的指针。

指向一个缓冲区，这个缓冲区存放的是readv所接收的数据或是writev将要发送的数据。

* size_t iov_len：缓冲区的大小（以字节为单位）

确定接收的最大长度或者实际写入的长度。

## 使用该结构体的函数
通常，这个结构用作一个多元素的数组。

利用readv和writev函数在一次函数调用中读、写多个非连续缓冲区。有时也将这两个函数称为散布读（scatter read）和聚集写（gather write），分散是指在读操作时将读取的数据写入多个buffer中，聚集是指在写操作时将多个buffer的数据写入同一个Channel

```c
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
 
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```
#### 参数

* fd是要在其上进行读或写的文件描述符
* iov是读或写所用的I/O向量机制结构体iovec
* iovcnt为结构体的个数
#### 返回值
成功则返回readv所读取的字节数或是writev所写入的字节数，若出错则返回-1，并设置errno

注意，与其他I/O函数类似，它们可以返回错误码EINTR来表明他被一个信号所中断。
#### 函数功能
writev以顺序iov[0]，iov[1]至iov[iovcnt-1]从缓冲区中聚集输出数据。writev返回输出的字节总数，通常，它应等于所有缓冲区长度之和。

`特别注意： 循环调用writev时，需要重新处理iovec中的指针和长度，该函数不会对这两个成员做任何处理。`writev的返回值为已写的字节数，但这个返回值“实用性”并不高，因为参数传入的是iovec数组，计量单位是iovcnt，而不是字节数，我们仍然需要通过遍历iovec来计算新的基址，另外写入数据的“结束点”可能位于一个iovec的中间某个位置，因此需要调整临界iovec的io_base和io_len。

