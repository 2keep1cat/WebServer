#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部静态变量懒汉不用加锁
    static Log *get_instance()
    {   
        //创建Log类的局部静态变量，实现单例模式
        static Log instance;
        return &instance;
    }
    //异步写日志公有方法，调用私有方法async_write_log
    static void *flush_log_thread(void *args)
    {
        //调用单例Log对象的私有方法async_write_log()
        Log::get_instance()->async_write_log();
    }
    //实现日志创建、写入方式的判断,可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    //完成写入日志文件中的具体内容，将输出内容按照标准格式整理，主要实现日志分级、分文件、格式化输出内容
    void write_log(int level, const char *format, ...);
    //强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();
    //异步写日志方法
    void *async_write_log()
    {
        string single_log;
        //从日志阻塞队列中不断取出日志字符串赋给single_log，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            //通过c_str()函数将single_log转换为C字符串，m_ fp代表了要写入的目标文件
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是哪一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;        //要输出的内容
    block_queue<string> *m_log_queue; //日志字符串组成的阻塞队列
    bool m_is_async;    //是否同步标志位
    locker m_mutex;
};

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif
