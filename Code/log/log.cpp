#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()//-------------构造函数
{
    m_count = 0;//-------日志行数清0
    m_is_async = false;//默认设置为同步
}

Log::~Log()//------------析构函数
{
    if (m_fp != NULL)//--如果文件指针不为空
    {
        fclose(m_fp);//--就把该文件指针关闭
    }
}
//根据参数设置日志记录器的属性，并根据是否使用异步写入方式进行相应的初始化工作。
//如果使用异步方式，会创建一个新线程用于异步写入日志。
//最后，根据日期和文件名生成完整的日志文件名，并打开该文件用于写入日志
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    if (max_queue_size >= 1)//----------------------------------max_queue_size参数默认为0，若传入了非0参数
    {
        m_is_async = true;//------------------------------------则设置写入方式为异步
        m_log_queue = new block_queue<string>(max_queue_size);//创建并设置阻塞队列长度
        pthread_t tid;
        //创建一个新线程，并将其ID存储在tid中。新线程的入口点函数是flush_log_thread，在log.h中定义，
        pthread_create(&tid, NULL, flush_log_thread, NULL);//---flush_log_thread为回调函数,这里表示创建线程来异步写日志
    }
    
    m_log_buf_size = log_buf_size;//----------------------------设置日志缓冲区的大小为传入的参数log_buf_size，默认为8192
    m_buf = new char[m_log_buf_size];//-------------------------分配一个大小为 m_log_buf_size 的字符数组作为日志缓冲区
    memset(m_buf, '\0', m_log_buf_size);//----------------------将日志缓冲区的内容初始化为 null 字符
    m_split_lines = split_lines;//------------------------------设置日志的最大行数

    time_t t = time(NULL);//------------------------------------获取当前时间的时间戳
    struct tm *sys_tm = localtime(&t);//------------------------将时间戳转换为本地时间
    struct tm my_tm = *sys_tm;//--------------------------------将本地时间保存在 my_tm 结构体中

    const char *p = strrchr(file_name, '/');//------------------在 file_name 字符串中查找最后一个 / 字符的位置
    char log_full_name[256] = {0};//----------------------------定义一个长度为 256 的字符数组 log_full_name，用于存储完整的日志文件名

    //相当于自定义日志名
    if (p == NULL)//--------------------------------------------如果找不到 / 字符说明file_name中不包含路径信息，
    {              //-------------------------------------------直接将时间+文件名作为日志名
        //将日期和 file_name 组合成一个完整的日志文件名，并将结果存储在 log_full_name 中
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else//------------------------------------------------------如果找到了 / 字符，说明file_name中包含路径信息
    {   
        strcpy(log_name, p + 1);//------------------------------从 / 字符的下一个位置开始的字符串复制到 log_name 变量中，即提取出文件名部分
        strncpy(dir_name, file_name, p - file_name + 1);//------p - file_name + 1是文件所在路径文件夹的字符长度，将file_name中路径部分的内容复制到dir_name变量中
        //将日期、路径和文件名组合成一个完整的日志文件名，并将结果存储在 log_full_name 中
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;//----------------------------------将当前日期存储在 m_today 变量中

    m_fp = fopen(log_full_name, "a");//-------------------------以追加模式打开指定的日志文件
    if (m_fp == NULL)//-----------------------------------------如果打开文件失败，则返回 false
    {
        return false;
    }

    return true;
}
//用于写入日志
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};//-----------定义一个 timeval 结构体变量 now，用于存储当前时间
    gettimeofday(&now, NULL);//--------------获取当前时间并存储在 now 变量中
    time_t t = now.tv_sec;//-----------------获取时间戳
    struct tm *sys_tm = localtime(&t);//-----将时间戳转换为本地时间
    struct tm my_tm = *sys_tm;//-------------将本地时间保存在 my_tm 结构体中
    char s[16] = {0};//----------------------定义一个长度为 16 的字符数组 s，用于存储“日志级别”的字符串
    switch (level)//-------------------------根据传入的日志级别level进行判断
    {
    case 0://--------------------------------如果 level 的值为 0，表示日志级别为调试（debug）
        strcpy(s, "[debug]:");//-------------将字符串 "[debug]:" 复制到 s 变量中
        break;
    case 1://--------------------------------如果 level 的值为 1，表示日志级别为信息（info）
        strcpy(s, "[info]:");
        break;
    case 2://--------------------------------如果 level 的值为 2，表示日志级别为警告（warn）
        strcpy(s, "[warn]:");
        break;
    case 3://--------------------------------如果 level 的值为 3，表示日志级别为错误（erro）
        strcpy(s, "[erro]:");
        break;
    default://-------------------------------如果 level 的值不在上述范围内，默认日志级别为信息（info）
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++
    m_mutex.lock();//------------------------获取互斥锁，用于保护临界区
    m_count++;//-----------------------------日志行数计数器加一
                            //---------------如果m_today记录的不是今天或者日志行数是最大行的倍数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        
        char new_log[256] = {0};//-----------定义一个长度为 256 的字符数组 new_log，用于存储新的日志文件名
        fflush(m_fp);//----------------------刷新文件流，将缓冲区的数据写入文件
        fclose(m_fp);//----------------------关闭当前打开的日志文件
        char tail[16] = {0};//---------------定义一个长度为 16 的字符数组 tail，用于存储日期后缀
        //将日期格式化为字符串，并存储在 tail 变量中
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        
        if (m_today != my_tm.tm_mday)//------如果m_today不等于今天，则创建今天的日志，更新m_today和m_count
        {   //将日志文件的路径、日期后缀和文件名拼接成新的日志文件名，并存储在 new_log 变量中
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;//-------更新m_today为当前日期
            m_count = 0;//-------------------重置日志行数计数器为0
        }
        else//-------------------------------如果m_today是今天，那么就是超过了最大行m_count % m_split_lines == 0
        {   //还是创建新的日志，新日志的文件名加上文件序号，文件序号由m_count / m_split_lines计算得到
            //将日志文件的路径、日期后缀、文件名和文件序号拼接成新的日志文件名，并存储在 new_log 变量中
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");//--------然后以追加模式打开新的日志文件
    }
 
    m_mutex.unlock();//----------------------释放互斥锁

    va_list valst;//-------------------------定义一个 va_list 类型的变量 valst，用于处理可变参数
    va_start(valst, format);//---------------初始化 valst，将其指向可变参数列表的起始位置，format 是可变参数之前的最后一个已知参数

    string log_str;//------------------------定义一个 string 类型的变量 log_str，用于存储日志内容
    m_mutex.lock();//------------------------获取互斥锁，保护临界区

    //将日期、时间、日志级别等信息格式化为字符串，存储在 m_buf 中，并返回格式化后的字符数
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //将可变参数 format 格式化为字符串，并将其追加到 m_buf 的末尾，返回格式化后的字符数
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';//-------------------在日志内容末尾添加换行符
    m_buf[n + m + 1] = '\0';//---------------在日志内容末尾添加字符串结束符
    log_str = m_buf;//-----------------------将 m_buf 中的日志内容赋值给 log_str

    m_mutex.unlock();//----------------------释放互斥锁

    if (m_is_async && !m_log_queue->full())//如果日志以异步方式写入且日志队列未满
    {
        m_log_queue->push(log_str);//--------将日志内容 log_str 推入队列
    }
    else//-----------------------------------否则是同步方式，直接写入日志文件
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);//------将日志内容 log_str 写入日志文件
        m_mutex.unlock();
    }

    va_end(valst);//-------------------------结束对可变参数的处理，释放相关资源
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
