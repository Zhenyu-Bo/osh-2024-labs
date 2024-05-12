# 指定编译器
CC = gcc
# 指定编译选项
CFLAGS = -Wall -pthread
# 指定目标文件
all: server
io: server_select
# 生成目标文件
server: server.c
	$(CC) $(CFLAGS) -o server server.c
server_select: server_select.c
	$(CC) $(CFLAGS) -o server_select server_select.c
# 清理目标文件
clean:
	rm -f server
	rm -f server_select