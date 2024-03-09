# 定义编译器和编译选项
CC = g++
CFLAGS = -g -O2
VERSION = -std=c++11
DEBUG = -DPRINT

# 定义目标文件和源文件
TARGET = bubble_sort
SRCS = bubblesort.cpp main.cpp

# 生成可执行文件
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(VERSION) -o $@ $^

# 定义make debug，编译时加入PRINT宏
debug:
	$(CC) $(CFLAGS) $(VERSION) $(DEBUG) -o $(TARGET) $(SRCS)

# 清理文件
clean:
	rm -f $(TARGET)