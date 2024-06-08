#include "http_conn.h"

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

/*epoll相关的函数*/
int setnonblocking(int fd){//--------------------对文件描述符设置非阻塞
    int old_option = fcntl(fd, F_GETFL);//-------先用fcntl获取文件描述符fd的文件状态标志
    int new_option = old_option | O_NONBLOCK;//--将获取到的文件状态标志位或上非阻塞
    fcntl(fd, F_SETFL, new_option);//------------再将新的文件状态标志位用fcntl设置到文件描述符fd中
    return old_option;//-------------------------返回旧的文件状态标志位
}
void addfd(int epollfd, int fd, bool one_shot){//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
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
void removefd(int epollfd, int fd){//------------从内核事件表删除文件描述符
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
void modfd(int epollfd, int fd, int ev){//-------修改监听事件为ev，并将事件重置为EPOLLONESHOT
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
void http_conn::init(int sockfd, const sockaddr_in &addr){//初始化连接,外部调用初始化套接字地址

}
void http_conn::close_conn(bool real_close){//关闭连接，关闭一个连接，客户总量减一

}
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
bool http_conn::read_once(){//循环读取客户数据，直到无数据可读或对方关闭连接,非阻塞ET工作模式下，需要一次性将数据读完

}
bool http_conn::write(){//写入响应报文

}
void http_conn::initmysql_result(connection_pool *connPool){

}
/*private中的函数*/
void http_conn::init(){//对私有成员变量进行初始化,check_state默认为分析请求行状态

}
/*此时从状态机已提前将一行的末尾字符\r\n变为\0\0（\0表示字符串的结尾），所以text可以直接取出完整的行进行解析*/
http_conn::HTTP_CODE http_conn::process_read(){//--通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理
    LINE_STATUS line_status = LINE_OK;//-----------初始化从状态机状态
    HTTP_CODE ret = NO_REQUEST;//------------------初始化HTTP请求解析结果
    char *text = 0;//------------------------------定义text指针变量用于存储读取的字符

    /*说明：判断条件中用||隔开的两个条件，如果第一个满足了就不会执行第二个
    除了请求内容（请求正文）的末尾没有\r\n，其它每行都有，而GET请求报文没有请求内容，所以对报文进行拆解时，仅用从状态机的状态line_status=parse_line())==LINE_OK语句即可
    但在POST请求报文中，请求内容的末尾没有任何字符，用parse_line()函数会返回LINE_OPEN，如果继续用该条件判断是否进入循环就会丢弃最后一行的请求内容，
    这里转而使用主状态机的状态m_check_state作为循环入口条件，不过解析完请求正文后m_check_state还是CHECK_STATE_CONTENT，为了防止再次进入循环，
    增加line_status == LINE_OK判断，在完成请求内容解析后，line_status==LINE_OPEN，从而跳出循环。
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
bool http_conn::process_write(HTTP_CODE ret){//向写缓冲区写入响应报文

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
http_conn::HTTP_CODE http_conn::do_request(){//生成响应报文

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
void http_conn::unmap(){

}
/*生成响应报文的8个部分，以下函数均由do_request()调用*/
bool http_conn::add_response(const char *format, ...){

}
bool http_conn::add_content(const char *content){

}
bool http_conn::add_status_line(int status, const char *title){

}
bool http_conn::add_headers(int content_length){

}
bool http_conn::add_content_type(){
    
}
bool http_conn::add_content_length(int content_length){
    
}
bool http_conn::add_linger(){
    
}
bool http_conn::add_blank_line(){
    
}