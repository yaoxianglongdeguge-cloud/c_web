# 编译器
CC = gcc

# 编译选项
CFLAGS = -g -O0 -Wall

# 链接选项
LDFLAGS = -lpthread

# 目标程序
TARGET = main

# 自动查找所有 .c 文件
SRCS = $(wildcard *.c */*.c)
OBJS = $(SRCS:.c=.o)

# 编译规则
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译 .c 为 .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(TARGET) $(OBJS)

# 运行
run: $(TARGET)
	./$(TARGET)

# 调试
debug: $(TARGET)
	gdb ./$(TARGET)

# 内存检查
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

.PHONY: clean run debug valgrind