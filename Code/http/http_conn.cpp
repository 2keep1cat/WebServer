#include "http_conn.h"
//#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>


//#define connfdET //-------边缘触发非阻塞
#define connfdLT //---------水平触发阻塞

//#define listenfdET //-----边缘触发非阻塞
#define listenfdLT //-------水平触发阻塞

/*定义http响应的一些状态信息*/
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//网站根目录，文件夹内存放请求的资源和跳转的html文件，当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/zhuwenjie/WebServer/resource";

map<string, string> users;//将表中的用户名和密码放入map，#考虑一下用是否可以换成unordered_map
locker m_lock;//------------插入新用户时用于保护users的锁

/*epoll相关的函数*/

/*对文件描述符设置非阻塞*/
int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);//-------先用fcntl获取文件描述符fd的文件状态标志
    int new_option = old_option | O_NONBLOCK;//--将获取到的文件状态标志位或上非阻塞
    fcntl(fd, F_SETFL, new_option);//------------再将新的文件状态标志位用fcntl设置到文件描述符fd中
    return old_option;//-------------------------返回旧的文件状态标志位
}

/*向内核事件表注册读事件，ET模式，选择是否开启EPOLLONESHOT*/
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;//-------------------------定义epoll_event类型变量event，作为传入epoll_ctl函数的事件
    event.data.fd = fd;//------------------------将fd赋给event事件
#ifdef connfdET//--------------------------------如果使用ET触发模式，将监听事件注册为读事件，ET模式，对方关闭连接
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
#ifdef connfdLT//--------------------------------如果使用LT触发模式，将监听事件注册为读事件，LT模式（默认），对方关闭连接
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
#ifdef listenfdET//------------------------------如果使用ET触发模式，将监听事件注册为读事件，ET模式，对方关闭连接
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
#ifdef listenfdLT//------------------------------如果使用LT触发模式，将监听事件注册为读事件，LT模式（默认），对方关闭连接
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
    if(one_shot)
        event.events |= EPOLLONESHOT;//----------如果传入的oneshot参数是true，就向事件中添加EPOLLONESHOT
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);//用epoll_ctl函数将epoll实例中添加fd，添加的事件为上面设置好的event
    setnonblocking(fd);//------------------------设置文件描述符非阻塞
}

/*从内核事件表删除文件描述符*/
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

/*修改监听事件为ev，并将事件重置为EPOLLONESHOT*/
void modfd(int epollfd, int fd, int ev){
    epoll_event event;//-------------------------定义epoll_event类型变量event，作为传入epoll_ctl函数的事件
    event.data.fd = fd;//------------------------将fd赋给event事件
#ifdef connfdET//--------------------------------如果使用ET触发模式，ev+ET+oneshot+rdhup
    event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
#endif
#ifdef connfdLT//--------------------------------如果使用ET触发模式，ev+oneshot+rdhup
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif 
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);//epoll_ctl
}

/*类中的函数*/

/*public中的函数*/

/*初始化连接,外部调用初始化套接字地址*/
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

/*关闭连接，关闭一个连接，客户总量减一*/
void http_conn::close_conn(bool real_close){
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/*工作线程调用来处理请求报文并向写缓冲区中写入响应报文*/
void http_conn::process(){
    HTTP_CODE read_ret = process_read();//-----调用process_read函数对报文进行解析，并返回HTTP_CODE型的变量用于接收解析结果
    if(read_ret == NO_REQUEST){//--------------如果解析结果是无请求，那么就监听一次它的读事件（为了等待下一次读取客户端数据的机会）
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return; //-----------------------------退出函数
    }
    bool write_ret = process_write(read_ret);//调用process_write函数写响应报文
    if(!write_ret){//--------------------------如果写响应报文失败，关闭连接
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT); //-----监听一次它的写事件
}

/*循环读取客户数据，直到无数据可读或对方关闭连接,非阻塞ET工作模式下，需要一次性将数据读完*/
bool http_conn::read_once(){
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0)
    {
        return false;
    }

    return true;

#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

/*写入响应报文，#此为书中原代码，请求较大文件时会报错*/
bool http_conn::write(){
    int temp = 0;//------------------------------------temp为发送的字节数

    if (bytes_to_send == 0)//--------------------------若未发送的字节数为0表示响应报文为空，一般不会出现这种情况
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);//---------修改监听事件为读
        init();//--------------------------------------然后调用 init() 函数重置连接状态
        return true;
    }

    while (1)//----------------------------------------如果待发送的字节数不为零，进入循环
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);//---将响应报文的状态行、消息头、空行和文件发送给浏览器端

        if (temp < 0)//--------------------------------如果writev()的返回值temp小于 0，表示发送出现错误
        {
            if (errno == EAGAIN)//---------------------如果errno=EAGAIN，表示发送缓冲区已满，需要等待下次可写事件
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);//修改监听事件为写
                return true;
            }
            unmap();
            return false;
        }
        /*如果 writev() 的返回值大于等于 0，表示发送成功*/
        bytes_have_send += temp;//---------------------更新已发送字节
        bytes_to_send -= temp;//-----------------------更新待发送字节数
        /*根据已发送字节数的情况，更新 m_iv 结构体数组中的数据*/
        if (bytes_have_send >= m_iv[0].iov_len)//------如果已发送字节数>=第一个数据块的长度，说明第一个数据块已经完全发送
        {
            m_iv[0].iov_len = 0;//---------------------将其长度置为 0
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;//---------更新第二个数据块的起始位置和长度,m_write_idx为第一个数据块的大小
        }
        else//-----------------------------------------否则表示第一个数据块还没发送完毕
        {                                   //---------更新第一个数据块的起始位置和长度
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        /*检查待发送字节数是否小于等于 0*/
        if (bytes_to_send <= 0)//----------------------如果是，表示所有数据已经发送完毕
        {
            unmap();//---------------------------------调用 unmap() 函数解除文件和内存的映射
            modfd(m_epollfd, m_sockfd, EPOLLIN);//-----修改监听事件为读
            /*然后根据是否需要保持连接（m_linger）来执行不同的操作*/
            if (m_linger)//----------------------------如果需要保持连接
            {
                init();//------------------------------重置连接状态并返回 true
                return true;
            }
            else//-------------------------------------如果不需要保持连接，返回false
            {
                return false;
            }
        }
    }
}

/*从MySQL数据库中查询user表中的数据，将用户名和密码存储到一个std::map对象中，以便后续在处理HTTP请求时进行用户身份验证*/
void http_conn::initmysql_result(connection_pool *connPool){
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        //LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        printf("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

/*private中的函数*/

/*对私有成员变量进行初始化,check_state默认为分析请求行状态*/
void http_conn::init(){
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*此时从状态机已提前将一行的末尾字符\r\n变为\0\0（\0表示字符串的结尾），所以text可以直接取出完整的行进行解析*/
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;//-----------初始化从状态机状态
    HTTP_CODE ret = NO_REQUEST;//------------------初始化HTTP请求解析结果
    char *text = 0;//------------------------------定义text指针变量用于存储读取的字符

    /*说明：判断条件中用||隔开的两个条件，如果第一个满足了就不会执行第二个
    除了请求内容（请求正文）的末尾没有\r\n，其它每行都有，而GET请求报文没有请求内容，所以对报文进行拆解时，仅用从状态机的状态line_status=parse_line())==LINE_OK语句即可
    但在POST请求报文中，请求内容的末尾没有任何字符，用parse_line()函数会返回LINE_OPEN，如果继续用该条件判断是否进入循环就会丢弃最后一行的请求内容，
    这里转而使用主状态机的状态m_check_state作为循环入口条件，不过解析完请求正文后m_check_state还是CHECK_STATE_CONTENT，为了防止再次进入循环，
    增加line_status == LINE_OK判断，在完成请求内容解析后，line_status==LINE_OPEN，从而跳出循环。
    通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理
    */
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)){
        
        text = get_line();//-----------------------用get_line函数返回要读取的位置的行首
        m_start_line = m_checked_idx;//------------m_start_line是每一个数据行在m_read_buf中的起始位置
        //-----------------------------------------m_checked_idx表示从状态机在m_read_buf中读取的位置
        //LOG_INFO("%s", text);//------------------暂时注释掉，因为还没写log
        //Log::get_instance()->flush();
        switch (m_check_state)//-------------------主状态机的三种状态转移逻辑
        {
            case CHECK_STATE_REQUESTLINE://--------解析请求行
            {
                ret = parse_request_line(text);//--调用解析请求行函数
                if (ret == BAD_REQUEST) //---------如果函数返回BAD_REQUEST，表示HTTP请求报文有语法错误
                    return BAD_REQUEST;
                break; //--------------------------否则跳出switch判断，继续while循环读取解析剩下的请求报文
            }
            case CHECK_STATE_HEADER://-------------解析请求头部
            {
                ret = parse_headers(text);//-------调用解析请求头部的函数
                if (ret == BAD_REQUEST) //---------如果函数返回BAD_REQUEST，表示HTTP请求报文有语法错误
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)//-----如果函数返回GET_REQUEST，表示获得了完整的GET请求报文
                {
                    return do_request();//---------那么就调用do_request函数生成响应报文
                }
                break;//---------------------------否则跳出switch判断，继续while循环读取解析剩下的请求报文
            }
            case CHECK_STATE_CONTENT://------------解析消息内容（请求正文）
            {
                ret = parse_content(text);//-------调用解析请求正文函数
                if (ret == GET_REQUEST) //---------如果函数返回GET_REQUEST，表示获得了完整的POST请求报文
                    return do_request();//---------那么就调用do_request函数生成响应报文
                line_status = LINE_OPEN;//---------把从状态机的状态设置为LINE_OPEN
                break;
            }
            default:
                return INTERNAL_ERROR;//-----------HTTP_CODE中的，表示服务器内部错误，一般不会触发
        }
    }
    return NO_REQUEST;//---------------------------如果之前都没有返回，说明请求不完整
}

/*传入peocess_write()函数的参数是process_read()函数的返回值，是其解析请求报文的HTTP_CODE结果，
而process_read()函数的返回值有部分是do_request()返回的，process()中调用process_write向m_write_buf中写入响应报文*/
bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)//--------------------------------------根据报文解析的HTTP_CODE结果进行如下处理
    {
        case INTERNAL_ERROR://--------------------------如果是服务器内部错误
        {
            add_status_line(500, error_500_title);//----添加状态行"Internal Error"
            add_headers(strlen(error_500_form));//------添加响应头部，将字符串的字符个数作为响应头部的 Content-Length 字段的值
            if (!add_content(error_500_form))//---------添加响应正文，如果出错返回错误
                return false;
            break;
        }
        case BAD_REQUEST://-----------------------------如果是请求报文有语法错误或请求资源为目录
        {
            add_status_line(404, error_404_title);//----添加状态行"Not Found"
            add_headers(strlen(error_404_form));//------添加响应头部，将字符串的字符个数作为响应头部的 Content-Length 字段的值
            if (!add_content(error_404_form))//---------添加响应正文，如果出错返回错误
                return false;
            break;
        }
        case FORBIDDEN_REQUEST://-----------------------如果请求资源禁止访问
        {
            add_status_line(403, error_403_title);//----添加状态行"Forbidden"
            add_headers(strlen(error_403_form));//------添加响应头部，将字符串的字符个数作为响应头部的 Content-Length 字段的值
            if (!add_content(error_403_form))//---------添加响应正文，如果出错返回错误
                return false;
            break;
        }
        case FILE_REQUEST://----------------------------如果请求资源可以正常访问
        {
            add_status_line(200, ok_200_title);//-------添加状态行"OK"
            if (m_file_stat.st_size != 0)//-------------如果文件大小不为0
            {
                add_headers(m_file_stat.st_size);//-----添加响应头部，将文件大小作为响应头部的 Content-Length 字段的值
                m_iv[0].iov_base = m_write_buf;//-------m_iv[0]缓冲区的指针指向写缓冲区，用于存储服务器发出的响应报文数据
                m_iv[0].iov_len = m_write_idx;//--------该缓冲区的大小
                m_iv[1].iov_base = m_file_address;//----m_iv[1]缓冲区的指针指向读取的文件在服务器的内存上的地址
                m_iv[1].iov_len = m_file_stat.st_size;//该缓冲区的大小设置为文件的大小
                m_iv_count = 2;//-----------------------表示 m_iv 数组中有效元素的个数为2
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;                          //需要发送的总字节数（响应报文数据 + 浏览器请求的文件内容数据）
            }
            else//--------------------------------------如果文件大小为 0（即文件存在但为空）
            {                                       //--建一个字符串ok_string，内容为一个简单的 HTML 页面，表示文件为空
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));//-------添加响应头部，将ok_string大小作为响应头部的 Content-Length 字段的值
                if (!add_content(ok_string))//----------添加响应正文，如果出错返回错误
                    return false;
            }
        }
        default:
            return false;
    }
    /*INTERNAL_ERROR，BAD_REQUEST，FORBIDDEN_REQUEST，FILE_REQUEST但是文件为空，这些情况如果响应报文添加成功就会执行下面的代码*/
    m_iv[0].iov_base = m_write_buf;//-------------------m_iv[0]缓冲区的指针指向写缓冲区，用于存储服务器发出的响应报文数据
    m_iv[0].iov_len = m_write_idx;//--------------------该缓冲区的大小
    m_iv_count = 1;//-----------------------------------表示 m_iv 数组中有效元素的个数为1
    bytes_to_send = m_write_idx;//----------------------需要发送的总字节数（只有响应报文数据，不包含文件数据）
    return true;//--------------------------------------虽然没有返回的文件，但是只要返回了响应报文就算true
}

/*主状态机解析http请求行，获得请求方法，目标url及http版本号；
在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    m_url = strpbrk(text, " \t");//--------------strpbrk()函数用于搜索text中有无" \t"的任一字符即空格和\t都行，成功则返回匹配位置的指针，失败返回NULL
    if (!m_url)//--------------------------------如果返回字符为NULL，则报告有语法错误
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';//---------------------------把空格或\t换成\0，指针后移一位，用于将请求方法取出
    char *method = text;
    if (strcasecmp(method, "GET") == 0)//--------如果text和"GET"字符串相等，设置请求方法为GET
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)//--如果text和"POST"字符串相等，设置请求方法为POST
    {
        m_method = POST;
        cgi = 1;//-------------------------------cgi设置为1，标志启用POST
    }
    else
        return BAD_REQUEST;//--------------------不为"GET"也不为"POST"则返回语法错误
    /*strspn()的作用是从m_url的第一个在字符开始，判断该字符是不是在参数2表示的集合中，即：是不是空格或制表符。
    直到不是空格或制表符，返回是空格或制表符的字符个数*/
    m_url += strspn(m_url, " \t");//-------------m_url前面已经跳过了请求方法和url之间的第一个空格，但不知道后面还有没有空格
                                  //-------------所以将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向url的第一个字符
    m_version = strpbrk(m_url, " \t");//---------然后用m_version指向下一个空格或制表符(url与http版本号之间的空格)
    if (!m_version)//----------------------------如果返回字符为NULL，则报告有语法错误
        return BAD_REQUEST;
    *m_version++ = '\0';//-----------------------把空格或\t换成\0，指针后移一位，用于将url取出
    m_version += strspn(m_version, " \t");//-----将m_version向后偏移，继续跳过空格和\t字符，指向http版本号的第一个字符
    if (strcasecmp(m_version, "HTTP/1.1") != 0)//如果不是http1.1版本，返回语法错误
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)//-如果m_url的前7个字符是http://，那么
    {
        m_url += 7;//----------------------------m_url向后移7位
        m_url = strchr(m_url, '/');//------------在m_url中找'/'第一次出现的位置，然后将m_url指向该位置
    }

    if (strncasecmp(m_url, "https://", 8) == 0)//如果m_url的前7个字符是https://，那么
    {
        m_url += 8;//----------------------------m_url向后移8位
        m_url = strchr(m_url, '/');//------------在m_url中找'/'第一次出现的位置，然后将m_url指向该位置
    }

    if (!m_url || m_url[0] != '/')//-------------如果m_url为空，或者m_url没有指向'/'字符，那么返回语法错误
        return BAD_REQUEST;
    
    if (strlen(m_url) == 1)//--------------------如果m_url只有'/'时，显示判断界面
        strcat(m_url, "judge.html");//-----------将字符串 "judge.html" 连接到 m_url 的末尾
    m_check_state = CHECK_STATE_HEADER;//--------把主状态机的状态改为解析请求头部
    return NO_REQUEST;//-------------------------返回请求不完整，表示还要继续读入请求报文
}

/*解析完请求行后，主状态机继续分析请求头和空行。
通过判断当前的text首位是不是\0字符，若是，则当前处理的是空行，若不是，则当前处理的是请求头*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    if (text[0] == '\0')//------------------------如果第一个字符是\0就是空行，如果有content_length就已经读过了
    {
        if (m_content_length != 0)//--------------如果content_length不为0就说明是post请求
        {
            m_check_state = CHECK_STATE_CONTENT;//可以去解析请求正文了
            return NO_REQUEST;//------------------请求不完整，需要继续解析
        }
        return GET_REQUEST;//---------------------如果是GET请求就说明解析完了，返回GET_REQUEST
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {                   //------------------------如果text的前11个字符是"Connection:"
        text += 11;//-----------------------------text向后移11位
        text += strspn(text, " \t");//------------将text跳过空格或者制表符
        if (strcasecmp(text, "keep-alive") == 0)//如果Connection:后面是keep-alive
        {
            m_linger = true;//--------------------是长连接，则将linger标志设置为true
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {                       //--------------------如果text的前15个字符是"Content-length:"
        text += 15;//-----------------------------text向后移15位
        text += strspn(text, " \t");//------------将text跳过空格或者制表符
        m_content_length = atol(text);//----------将数字字符串转换为长整型数字，记录下content_length
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {               //----------------------------如果text的前5个字符是"Host:"
        text += 5;//------------------------------text向后移5位
        text += strspn(text, " \t");//------------将text跳过空格或者制表符
        m_host = text;//--------------------------记录下host，目标服务器的主机名或IP地址
    }
    else//----------------------------------------其它的请求头部则不管
    {
        printf("oop!unknow header: %s\n",text);
        //LOG_INFO("oop!unknow header: %s", text);
        //Log::get_instance()->flush();
    }
    return NO_REQUEST;//--------------------------如果未正常到尾部则返回请求不完整，需要继续解析
}

/*用于解析请求内容*/
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if (m_read_idx >= (m_content_length + m_checked_idx))//-m_read_idx为读缓冲区m_read_buf末尾的下一个位置，判断buffer中是否读取了消息体
    {                                                    //-正常情况下此时的m_checked_idx指向请求内容的第一个字符，如果判断为真，即buffer中读取了请求内容
        text[m_content_length] = '\0';//--------------------将请求内容尾部的后一个字符修改为\0，用于截断
        //--------------------------------------------------POST请求中最后为输入的用户名和密码
        m_string = text;//----------------------------------将请求内容赋值给m_string
        return GET_REQUEST;//-------------------------------获得了完整的请求
    }
    return NO_REQUEST;//------------------------------------如果buffer中没有完整读取请求内容，返回请求报文不完整
}

/*为生成响应报文做准备,该函数将网站根目录和url文件拼接，然后通过stat判断该文件属性
浏览器网址栏中的字符，即url，可以将其抽象成ip:port/xxx，xxx通过html文件的action属性进行设置
m_url为请求报文中解析出的请求资源，也就是/xxx，有8种情况*/
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);//----------------------------m_real_file是用来存储要读取文件的名称的，先将网站根目录赋值给它
    int len = strlen(doc_root);//-------------------------------len为根目录的字符个数
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/'); //---------------------找到m_url中/的位置，strrchr()函数用于在给定字符串中查找指定字符的最后一个匹配位置

    /*如果启用POST && '/'的下一个字符是'2'或'3'，对应的是/2CGISQL.cgi（登录校验）和/3CGISQL.cgi（注册校验）*/
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char flag = m_url[1];//---------------------------------flag存储'/'的下一个字符即'2'或'3'，判断是登录检测还是注册检测
                                
        char *m_url_real = (char *)malloc(sizeof(char) * 200);//新建一个字符串用于存储去掉数字后的m_url
        strcpy(m_url_real, "/");//------------------------------让m_url_real等于"/"
        strcat(m_url_real, m_url + 2);//------------------------将m_url_real接上m_url的第三个字符开始的后面所有字符
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
                                        //----------------------将m_url_real的 FILENAME_LEN-len-1个字符复制给m_real_file的第len+1个位置，
                                        //----------------------使得m_real_file = 网站根目录 + m_url_real
        free(m_url_real);//-------------------------------------释放m_url_real的内存空间，m_url_real的任务完成了

        /*将用户名和密码提取出来，user=123&passwd=123*/
        char name[100], password[100];//------------------------用于存储用户名和密码
        int i;
        for (i = 5; m_string[i] != '&'; ++i)//------------------m_string存储了请求内容
            name[i - 5] = m_string[i];//------------------------将第6个字符开始到'&'之前的字符赋值给name
        name[i - 5] = '\0';//-----------------------------------并在name末尾加\0结束

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)//-------将'&'字符后第10位开始到结束符
            password[j] = m_string[i];//------------------------之间的字符赋值给password
        password[j] = '\0';//-----------------------------------并在password末尾加\0结束

        if (*(p + 1) == '3')//----------------------------------如果是注册，先检测数据库中是否有重名的
        {
            /*先准备好要插入的字符串sql_insert*/
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");//--------------------------这一段的效果就是让sql_insert = 
            strcat(sql_insert, name);//-------------------------"INSERT INTO user(username, passwd) VALUES('name','password')"
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())//--------------如果在users中找不到name，即没有重名的
            {
                m_lock.lock();//--------------------------------先上锁使得其它线程无法执行下面的步骤
                int res = mysql_query(mysql, sql_insert);//-----将新用户的信息插入到数据库中
                users.insert(pair<string, string>(name, password));
                                                        //------将新用户的用户名 name 和密码 password 插入到 users 容器中
                m_lock.unlock();//------------------------------解锁

                if (!res)//-------------------------------------如果res为0，表示插入数据库成功，即用户注册成功
                    strcpy(m_url, "/log.html");//---------------将 m_url 字符数组设置为 "/log.html"，用于后续的重定向或页面跳转
                else    //--------------------------------------如果res不为0，表示插入操作失败，即用户注册失败
                    strcpy(m_url, "/registerError.html");//-----将 m_url 字符数组设置为 "/registerError.html"，用于后续的错误页面展示
            }
            else //---------------------------------------------如果在users中找到了name，表示已有重名用户
                strcpy(m_url, "/registerError.html");//---------将 m_url 字符数组设置为"/registerError.html"，用于后续的错误页面展示
        }

        
        else if (*(p + 1) == '2')//-----------------------------如果是登录
        {                                           //----------如果浏览器端输入的用户名和密码在表中可以查找到
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");//---------------将 m_url 字符数组设置为"/welcome.html"，用于展示欢迎界面
            else //---------------------------------------------如果浏览器端输入的用户名和密码在表中查找不到
                strcpy(m_url, "/logError.html");//--------------将 m_url 字符数组设置为"/logError.html"，用于展示登录出错界面
        }
    }

    if (*(p + 1) == '0')//--------------------------------------如果请求资源为/0，表示跳转注册界面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);//新建一个字符串
        strcpy(m_url_real, "/register.html");//-----------------m_url_real="/register.html"
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
                                                //--------------将m_url_real的FILENAME_LEN-len-1个字符复制给m_real_file的第len+1个位置，
                                                //--------------使得m_real_file = 网站根目录 + m_url_real
        free(m_url_real);//-------------------------------------释放m_url_real的内存空间，m_url_real的任务完成了
    }
    else if (*(p + 1) == '1')//---------------------------------如果请求资源为/1，表示跳转登录界面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);//新建一个字符串
        strcpy(m_url_real, "/log.html");//----------------------m_url_real="/log.html"
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
                                                //--------------将m_url_real的FILENAME_LEN-len-1个字符复制给m_real_file的第len+1个位置，
                                                //--------------使得m_real_file = 网站根目录 + m_url_real
        free(m_url_real);//-------------------------------------释放m_url_real的内存空间，m_url_real的任务完成了
    }
    else if (*(p + 1) == '5')//---------------------------------如果请求资源为/5，表示图片请求
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')//---------------------------------如果请求资源为/6，表示视频请求
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')//---------------------------------如果请求资源为/7，表示关注页面请求
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else //-----------------------------------------------------如果请求资源为/或/2或/3
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
                                                    //----------则让m_real_file = 网站根目录 + 请求资源,m_url为请求报文中解析出的请求资源，也就是/xxx
    
    if (stat(m_real_file, &m_file_stat) < 0)//------------------通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
        return NO_RESOURCE;//-----------------------------------失败返回NO_RESOURCE状态，表示资源不存在
    if (!(m_file_stat.st_mode & S_IROTH))//---------------------判断文件的权限，是否可读
        return FORBIDDEN_REQUEST;//-----------------------------不可读则返回FORBIDDEN_REQUEST状态
    if (S_ISDIR(m_file_stat.st_mode))//-------------------------判断文件类型，如果是目录
        return BAD_REQUEST;//-----------------------------------则返回BAD_REQUEST，表示请求报文有误
    int fd = open(m_real_file, O_RDONLY);//---------------------如果成功获取了文件信息，文件可读且不是目录，那么以只读方式打开文件获取文件描述符
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                                                            //--通过mmap将该文件映射到内存中
    close(fd);//------------------------------------------------避免文件描述符的浪费和占用
    return FILE_REQUEST;//--------------------------------------表示请求文件存在，且可以访问
}

/*从状态机，用于读取一行并把'\r\n'换成'\0\0'，返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN*/
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)//----m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
    {                                                  //----m_checked_idx指向从状态机当前正在分析的字节
        temp = m_read_buf[m_checked_idx];//------------------取出缓冲区m_read_buf中的一个字符
        if (temp == '\r')//----------------------------------如果该字符为'\r'（表示回车到行首），则判断是不是读取到完整行
        {
            if ((m_checked_idx + 1) == m_read_idx)//---------如果当前位置是最后一个字符，则接收不完整，需要继续接收
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')//如果不是最后一个字符，那么判断下一个字符是不是'\n'（表示换行）
            {
                m_read_buf[m_checked_idx++] = '\0';//--------如果是就把'\r\n'换成'\0\0'，并返回LINE_OK表示读完了这行
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;//-------------------------------如果读到字符'\r'但是既没有到末尾，后面也不是'\n'，那么请求报文错误
        }
        else if (temp == '\n')//-----------------------------如果该字符为'\n'，也有可能读取到了完整行，是上次读取到\r就到buffer末尾了
        {//--------------------------------------------------没有接收完整，再次接收时会出现这种情况，即上面返回LINE_OPEN的情况
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {                  //----------------------------如果上一个字符是'\r'，那么         
                m_read_buf[m_checked_idx - 1] = '\0';//------把'\r\n'换成'\0\0'，并返回LINE_OK表示读完了这行
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;//-------------------------------如果读到字符'\n'但是前一个字符不是'\r'，那么请求报文错误
        }
    }
    return LINE_OPEN;//--------------------------------------并没有找到\r\n，需要继续接收
}

/*将文件和内存的映射解除*/
void http_conn::unmap(){
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*生成响应报文的8个部分，以下函数均由do_request()调用*/
/*将数据写入写缓冲区，format 是一个格式化字符串，类似于 printf 函数的格式化参数*/
bool http_conn::add_response(const char *format, ...){
    if (m_write_idx >= WRITE_BUFFER_SIZE)//----------------------如果写入内容超出m_write_buf大小则报错
        return false;
    va_list arg_list;//------------------------------------------定义可变参数列表
    va_start(arg_list, format);//--------------------------------将变量arg_list初始化为传入参数,将其设置为 format 之后的可变参数
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
                                                    //-----------将可变参数列表中的数据按照 format 的格式写入到缓冲区中，返回写入的数据长度
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))//----------如果写入的数据长度超过缓冲区剩余空间，则报错
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;//----------------------------------------更新m_write_idx位置
    va_end(arg_list);//------------------------------------------清空可变参列表
    //LOG_INFO("request:%s", m_write_buf);
    //Log::get_instance()->flush();
    return true;
}

/*添加文本content，将 content 作为格式化字符串的参数传入*/
bool http_conn::add_content(const char *content){
    return add_response("%s", content);//------------------------调用add_response写响应正文，并返回是否写成功
}

/*添加状态行*/
bool http_conn::add_status_line(int status, const char *title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*添加消息报头*/
bool http_conn::add_headers(int content_length){
    add_content_length(content_length);
    add_linger();
    add_blank_line();
}

/*添加文本类型，这里是html*/
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
}

/*添加Content-Length，表示响应报文的长度*/
bool http_conn::add_content_length(int content_length){
    return add_response("Content-Length:%d\r\n", content_length);
}

/*添加连接状态，通知浏览器端是保持连接还是关闭*/
bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

/*添加空行*/
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}