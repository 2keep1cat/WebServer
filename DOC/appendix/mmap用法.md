# mmap
### 作用
用于将一个文件或其他对象映射到内存，提高文件的访问速度。
### 用法
```c
void* mmap(void* start,size_t length,int prot,int flags,int fd,off_t offset);

int munmap(void* start,size_t length);
```

* start：映射区的开始地址，设置为0时表示由系统决定映射区的起始地址

* length：映射区的长度

* prot：期望的内存保护标志，不能与文件的打开模式冲突

* PROT_READ 表示页内容可以被读取

* flags：指定映射对象的类型，映射选项和映射页是否可以共享

* MAP_PRIVATE 建立一个写入时拷贝的私有映射，内存区域的写入不会影响到原文件

* fd：有效的文件描述符，一般是由open()函数返回

* off_t offset：被映射对象的内容的起点