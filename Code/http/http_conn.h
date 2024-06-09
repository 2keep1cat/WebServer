#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/mman.h>

#include "../lock/lock.h"
#include "../mysql/sql_connection_pool.h"

class http_conn{
public:
    /*加static修饰的作用是在类的所有对象之间共享一个变量*/
    static const int READ_BUFFER_SIZE = 2048; //-设置读缓冲区m_read_buf大小，
    static const int WRITE_BUFFER_SIZE = 1024;//-设置写缓冲区m_write_buf大小
    static const int FILENAME_LEN = 200; //------设置读取文件的名称m_real_file大小

    /*在枚举中，每个枚举常量都会被赋予一个整数值，默认情况下从 0 开始递增。
    通过显式地将 GET 的值设置为 0，可以确保枚举常量的值从0开始递增*/
    enum METHOD{//-------------------------------定义枚举类型METHOD，表示http请求的不同方法
        GET=0,//---------------------------------GET请求
        POST,//----------------------------------POST请求
        HEAD,//----------------------------------HEAD请求
        PUT,//-----------------------------------PUT请求
        DELETE,//--------------------------------DELETE请求
        TRACE,//---------------------------------TRACE请求
        OPTIONS,//-------------------------------OPTIONS请求
        CONNECT,//-------------------------------CONNECT请求
        PATH//-----------------------------------PATCH请求
        };

    enum CHECK_STATE{ //-------------------------主状态机的状态，用于表示HTTP请求解析过程中的不同检查状态
        CHECK_STATE_REQUESTLINE=0,//-------------表示正在解析请求行
        CHECK_STATE_HEADER,//--------------------表示正在解析请求头部
        CHECK_STATE_CONTENT};//------------------表示正在解析请求内容

    enum HTTP_CODE //----------------------------定义枚举类型HTTP_CODE，表示报文解析的结果，表示HTTP请求的不同状态
    {
        NO_REQUEST, //---------------------------请求不完整，需要继续读取请求报文数据，跳转主线程继续监测读事件
        GET_REQUEST, //--------------------------获得了完整的HTTP请求，调用do_request完成请求资源映射
        BAD_REQUEST, //--------------------------HTTP请求报文有语法错误或请求资源为目录，跳转process_write完成响应报文
        NO_RESOURCE, //--------------------------请求资源不存在，跳转process_write完成响应报文
        FORBIDDEN_REQUEST, //--------------------请求资源禁止访问，没有读取权限，跳转process_write完成响应报文
        FILE_REQUEST, //-------------------------请求资源可以正常访问，跳转process_write完成响应报文
        INTERNAL_ERROR, //-----------------------服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION //---------------------连接关闭
    };

    enum LINE_STATUS //--------------------------从状态机的状态，用于表示处理文本行的不同状态
    {
        LINE_OK = 0, //--------------------------表示文本行处理成功，通常用于表示成功读取或处理了一行文本
        LINE_BAD, //-----------------------------表示文本行处理失败或出现错误，通常用于表示无效的或错误的文本行
        LINE_OPEN //-----------------------------表示文本行处于打开状态，通常用于表示正在处理文本行或准备读取下一行
    };

    /*函数声明*/
    http_conn(){}//-----------------------------------构造函数
    ~http_conn(){}//----------------------------------析构函数
    void init(int sockfd, const sockaddr_in &addr);//-初始化套接字地址
    void close_conn(bool real_close = true);//--------关闭http连接
    void process();//---------------------------------子线程通过process函数对任务队列中的任务进行处理
    bool read_once();//-------------------------------读取浏览器端发来的请求报文
    bool write();//-----------------------------------响应报文的写入函数
    sockaddr_in *get_address()//----------------------返回m_address的函数
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);//同步线程初始化数据库读取表,使用线程池初始化数据库表

    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;

private:
    void init();//-------------------------------------------对私有成员变量进行初始化
    HTTP_CODE process_read();//------------------------------从读缓冲区读取并解析请求报文
    bool process_write(HTTP_CODE ret);//---------------------向写缓冲区写入响应报文
    HTTP_CODE parse_request_line(char *text);//--------------主状态机解析报文中的请求行数据
    HTTP_CODE parse_headers(char *text);//-------------------主状态机解析报文中的请求头数据
    HTTP_CODE parse_content(char *text);//-------------------主状态机解析报文中的请求内容
    HTTP_CODE do_request();//--------------------------------生成响应报文
    char *get_line() { return m_read_buf + m_start_line; };//get_line用于将指针向后偏移，指向未处理的字符，m_start_line是已经解析的字符，是行在buffer中的起始位置
    LINE_STATUS parse_line();//------------------------------从状态机，用于把'\r\n'换成'\0\0'
    void unmap();
    /*生成响应报文的8个部分，以下函数均由do_request()调用*/
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
private:
    
    int m_sockfd;//------------------------套接字描述符，是每个打开的套接字的唯一标识符
    /*sockaddr_in 是一个用于表示 IPv4 地址信息的结构体，在网络编程中经常用于存储网络地址和端口信息*/
    sockaddr_in m_address;//---------------定义一个sockaddr_in类型的变量，
    char m_read_buf[READ_BUFFER_SIZE];//---存储读取的请求报文数据，即读缓冲区
    int m_read_idx;//----------------------读缓冲区m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;//-------------------m_checked_idx是读缓冲区m_read_buf中读取的位置
    int m_start_line;//--------------------是读缓冲区m_read_buf中已经解析的请求报文的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];//-写缓冲区，用于存储服务器发出的响应报文数据
    int m_write_idx;//---------------------指示读缓冲区中的长度
    
    CHECK_STATE m_check_state;//-----------主状态机的状态
    METHOD m_method;//---------------------请求方法
    
    /*以下为解析请求报文中对应的6个变量*/
    char m_real_file[FILENAME_LEN];//------存储读取文件的名称
    char *m_url;//-------------------------请求路径
    char *m_version;//---------------------http协议版本
    char *m_host;//------------------------目标服务器的主机名或IP地址
    int m_content_length;//----------------POST报文中的内容长度
    bool m_linger;

    
    char *m_file_address; //---------------读取的文件在服务器上的地址
    struct stat m_file_stat;//-------------stat是用于表示文件对象的结构体
    struct iovec m_iv[2]; //---------------io向量机制iovec
    int m_iv_count;
    int cgi; //----------------------------是否启用POST
    char *m_string; //---------------------存储请求内容
    int bytes_to_send; //------------------未发送的字节数
    int bytes_have_send; //----------------已发送的字节数
};

#endif