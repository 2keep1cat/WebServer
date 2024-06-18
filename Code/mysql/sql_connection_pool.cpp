#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	this->CurConn = 0;
	this->FreeConn = 0;
}
//单例模式，局部静态成员
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//初始化与数据库的连接
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{	
	//初始化数据库信息
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DBName;
	//创建MaxConn条数据库连接
	lock.lock();
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;//-----------------------声明一个指向 MYSQL 结构体类型的指针变量 con，并将其初始化为 NULL
		con = mysql_init(con);//-------------------分配并初始化一个新的MYSQL结构体，并返回指向该结构体的指针，这意味着 con 现在指向一个有效的、已初始化的 MYSQL 对象

		if (con == NULL)//-------------------------如果con为NULL表示在调用mysql_init()函数时发生了错误，无法初始化MYSQL对象
		{
			cout << "Error:" << mysql_error(con);//使用 mysql_error(con) 函数获取具体的错误描述
			exit(1);
		}
												 //通过参数中提供的信息，建立与对应数据库的连接
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)//-------------------------如果连接失败
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		connList.push_back(con);//-----------------如果连接成功建立，将 con 添加到 connList（连接列表）中
		++FreeConn;//------------------------------并增加 FreeConn（空闲连接）的计数
	}

	reserve = sem(FreeConn);//---------------------将信号量初始化为最大连接次数，reserve表示用来实现线程同步的信号量

	this->MaxConn = FreeConn;//--------------------记录MaxConn
	
	lock.unlock();
}


//获取连接，当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())//如果连接池中没有连接
		return NULL;

	reserve.wait();//----------如果连接池中存在连接，则申请信号量资源，信号量不为0则让信号量-1，为0则等待
	
	lock.lock();

	con = connList.front();//--将第一个连接取出
	connList.pop_front();

	--FreeConn;//--------------空闲连接数-1
	++CurConn;//---------------已使用的连接数+1，这里的两个变量并没有用到

	lock.unlock();
	return con;//--------------返回这个可用连接
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)//---------如果当前要释放的连接为空，则报错
		return false;

	lock.lock();

	connList.push_back(con);//-将该连接放回连接池
	++FreeConn;//--------------空闲连接数+1
	--CurConn;//---------------已使用的连接数-1

	lock.unlock();

	reserve.post();//----------信号量+1
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
	lock.lock();
	if (connList.size() > 0)//-------如果连接池的可用连接数不为0，则需要销毁
	{	
		list<MYSQL *>::iterator it;//创建一个连接池可以用的迭代器，通过迭代器遍历，关闭数据库连接
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;//------逐个取出连接池中的连接
			mysql_close(con);//------并关闭
		}
		CurConn = 0;
		FreeConn = 0;
		connList.clear();//----------清空list

		lock.unlock();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}
//RAII机制销毁连接池，自动化，不需要手动调用DestroyPool()函数
connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}