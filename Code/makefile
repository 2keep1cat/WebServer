server: main.c ./thread_pool/thread_pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./mysql/sql_connection_pool.cpp ./mysql/sql_connection_pool.h
	g++ -o server main.c ./thread_pool/thread_pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./log/log.cpp ./log/log.h ./mysql/sql_connection_pool.cpp ./mysql/sql_connection_pool.h -L/usr/lib64/mysql/  -lpthread -lmysqlclient


clean:
	-rm  -r server

