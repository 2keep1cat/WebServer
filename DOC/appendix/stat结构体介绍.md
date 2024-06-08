# struct stat结构体简介
在使用这个结构体和方法时，需要引入：
```c
#include <sys/types.h>
#include <sys/stat.h>
```
struct stat这个结构体是用来描述一个linux系统文件系统中的文件属性的结构体。

## 可以有两种方法来获取一个文件的属性：

### 1、通过路径
```c
int stat(const char *path, struct stat *struct_stat);

int lstat(const char *path,struct stat *struct_stat);
```
#### 参数
* 第一个参数是文件的路径
* 第二个参数是struct stat的指针。
#### 返回值
* 成功返回0，并将文件属性传入struct_stat结构体对象
* 失败返回-1，且error被自动设置为下面的值：

        EBADF： 文件描述词无效

        EFAULT： 地址空间不可访问

        ELOOP： 遍历路径时遇到太多的符号连接

        ENAMETOOLONG：文件路径名太长

        ENOENT：路径名的部分组件不存在，或路径名是空字串

        ENOMEM：内存不足

        ENOTDIR：路径名的部分组件不是目录

#### 两个系统调用的区别
`stat没有处理字符链接(软链接)的能力`

如果一个文件是符号链接，
* stat会直接返回它所指向的文件的属性；
* 而lstat返回的就是这个符号链接的内容。

>`软链接和硬链接的含义`：我们知道目录在linux中也是一个文件，文件的内容就是这这个目录下面所有文件与inode的对应关系。那么所谓的硬链接就是在某一个目录下面将一个文件名与一个inode关联起来，其实就是添加一条记录！而软链接也叫符号链接更加简单了，这个文件的内容就是一个字符串，这个字符串就是它所链接的文件的绝对或者相对地址。

### 2、通过文件描述符
```c
int fstat(int fdp, struct stat *struct_stat);　　//通过文件描述符获取文件对应的属性。fdp为文件描述符
```

下面是这个结构的结构
```c
struct stat {
        mode_t     st_mode;       //文件对应的模式，文件，目录等
        ino_t      st_ino;        //inode节点号
        dev_t      st_dev;        //设备号码
        dev_t      st_rdev;       //特殊设备号码
        nlink_t    st_nlink;      //文件的连接数
        uid_t      st_uid;        //文件所有者
        gid_t      st_gid;        //文件所有者对应的组
        off_t      st_size;       //普通文件，对应的文件字节数
        time_t     st_atime;      //文件最后被访问的时间
        time_t     st_mtime;      //文件内容最后被修改的时间
        time_t     st_ctime;      //文件状态改变时间
        blksize_t st_blksize;     //文件内容对应的块大小
        blkcnt_t   st_blocks;     //伟建内容对应的块数量
      };
```
stat结构体中的st_mode 则定义了下列数种情况：
```
    S_IFMT   0170000    文件类型的位遮罩
    S_IFSOCK 0140000    scoket
    S_IFLNK 0120000     符号连接
    S_IFREG 0100000     一般文件
    S_IFBLK 0060000     区块装置
    S_IFDIR 0040000     目录
    S_IFCHR 0020000     字符装置
    S_IFIFO 0010000     先进先出

    S_ISUID 04000     文件的(set user-id on execution)位
    S_ISGID 02000     文件的(set group-id on execution)位
    S_ISVTX 01000     文件的sticky位

    S_IRUSR(S_IREAD) 00400     文件所有者具可读取权限
    S_IWUSR(S_IWRITE)00200     文件所有者具可写入权限
    S_IXUSR(S_IEXEC) 00100     文件所有者具可执行权限

    S_IRGRP 00040             用户组具可读取权限
    S_IWGRP 00020             用户组具可写入权限
    S_IXGRP 00010             用户组具可执行权限

    S_IROTH 00004             其他用户具可读取权限
    S_IWOTH 00002             其他用户具可写入权限
    S_IXOTH 00001             其他用户具可执行权限
```
上述的文件类型在POSIX中定义了检查这些类型的宏定义：
```
    S_ISLNK (st_mode)    判断是否为符号连接
    S_ISREG (st_mode)    是否为一般文件
    S_ISDIR (st_mode)    是否为目录
    S_ISCHR (st_mode)    是否为字符装置文件
    S_ISBLK (s3e)        是否为先进先出
    S_ISSOCK (st_mode)   是否为socket
```
若一目录具有sticky位(S_ISVTX)，则表示在此目录下的文件只能被该文件所有者、此目录所有者或root来删除或改名，在linux中，最典型的就是/tmp目录