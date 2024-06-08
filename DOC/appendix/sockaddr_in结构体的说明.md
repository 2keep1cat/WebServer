`struct sockaddr_in` 是一个用于表示IP地址信息的结构体，在网络编程中经常用于存储网络地址和端口信息。它定义在 `<netinet/in.h>`或`<arpa/inet.h>` 头文件中。

该结构体包含以下成员：

```cpp
struct sockaddr_in {
    sa_family_t sin_family;    // 地址族，通常为 AF_INET
    uint16_t sin_port;    // 端口号
    struct in_addr sin_addr;    // IPv4 地址
    char sin_zero[8];    // 用于对齐，通常填充为 0
};
```

其中的in_addr结构体定义如下
```c
struct in_addr{
    In_addr_t s_addr; //32位IPv4地址
};
```
* sin_family 表示地址族，通常为 AF_INET 表示 IPv4 地址族。
* sin_port 表示端口号
* sin_addr 表示 IPv4 地址。
* sin_zero 用于对齐的填充字段，通常填充为 0。

sin_port和sin_addr都必须是网络字节序（NBO），一般可视化的数字都是主机字节序（HBO）


示例用法：

```cpp
#include <netinet/in.h>

struct sockaddr_in m_address;
m_address.sin_family = AF_INET;
m_address.sin_port = htons(8080);  // 设置端口号，使用 htons 进行字节序转换
m_address.sin_addr.s_addr = inet_addr("127.0.0.1");  // 设置 IPv4 地址
```
上述示例中，m_address 是一个 struct sockaddr_in 类型的变量，用于存储 IPv4 地址和端口信息