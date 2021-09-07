# main

ok: clean1

main: main.o mysql_connection_pool.o http_conn.o log.o lst_timer.o
	g++ main.o mysql_connection_pool.o http_conn.o log.o lst_timer.o -o main -lpthread -lmysqlclient


main.o: main.cpp ./connectionpool/mysql_connection_pool.h ./http/http_conn.h ./log/log.h ./threadpool/thread_pool.h ./timer/lst_timer.h
	g++ -c main.cpp -o main.o -lpthread -lmysqlclient

mysql_connection_pool.o: ./connectionpool/mysql_connection_pool.cpp ./connectionpool/mysql_connection_pool.h ./lock/locker.h
	g++ -c ./connectionpool/mysql_connection_pool.cpp -o mysql_connection_pool.o -lpthread -lmysqlclient

http_conn.o: ./http/http_conn.cpp ./http/http_conn.h ./connectionpool/mysql_connection_pool.h ./lock/locker.h ./log/log.h
	g++ -c ./http/http_conn.cpp -o http_conn.o -lpthread -lmysqlclient

log.o: ./log/log.cpp ./log/log.h ./log/block_queue.h
	g++ -c ./log/log.cpp -o log.o -lpthread -lmysqlclient

lst_timer.o: ./timer/lst_timer.cpp ./timer/lst_timer.h
	g++ -c ./timer/lst_timer.cpp -o lst_timer.o -lpthread -lmysqlclient


clean1: main
	rm -rf main.o mysql_connection_pool.o http_conn.o log.o lst_timer.o

clean:
	rm -rf main

