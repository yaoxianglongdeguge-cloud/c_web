# 编译器
CC = gcc

# 编译选项（-I. 是为了让 Include/Include.h 里的 #include "foo.h" 能命中库根目录的 foo.h）
CFLAGS = -g -O0 -Wall -I. -IAgentSpace

# 链接选项
LDFLAGS = -lpthread

# 目标程序
TARGET = main

# 静态库
LIB_TARGET = libcweb.a

# 自动查找所有 .c 文件（目录最深三层，如 FdConnectManage/Timer/TimerDataStruct/）
SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)
# 排除 test/ 下的用例源码（它们各自带 main，混进来会导致 multiple definition of `main`）
SRCS := $(filter-out test/% MemoryPool/OldPool/% AgentSpace/test/%,$(SRCS))
OBJS = $(SRCS:.c=.o)

# 编译规则
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译静态库
$(LIB_TARGET): $(OBJS)
	ar rcs $@ $^

# 一键编译静态库
lib: $(LIB_TARGET)

# 编译 .c 为 .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(TARGET) $(LIB_TARGET) $(OBJS)

# 运行
run: $(TARGET)
	./$(TARGET)

# 调试
debug: $(TARGET)
	gdb ./$(TARGET)

# 内存检查
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

.PHONY: clean run debug valgrind lib