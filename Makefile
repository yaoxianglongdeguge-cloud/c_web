# 编译器
CC = gcc

# 编译选项（-I. 是为了让 Include/Include.h 里的 #include "foo.h" 能命中库根目录的 foo.h）
# -MMD -MP：为每个 .o 生成 .d 头文件依赖，改结构体/头文件后 make 会自动重编相关 .o
# OPT：-O0 便于调试；压测/发布用 make OPT="-O2 -march=native"
OPT ?= -O0
CFLAGS = -g $(OPT) -Wall -I. -IAgentSpace -MMD -MP

# 链接选项
LDFLAGS = -lpthread

# 目标程序
TARGET = main

# 静态库
LIB_TARGET = libcweb.a

# 自动查找所有 .c 文件（目录最深三层，如 FdConnectManage/Timer/TimerDataStruct/）
SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)
# 排除 test/ 下的用例源码（它们各自带 main，混进来会导致 multiple definition of `main`）
SRCS := $(filter-out test/% MemoryPool/OldPool/% AgentSpace/test/% bench/%,$(SRCS))
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

# QPS 测试服务器（排除 AgentSpace/main.o，避免 main 冲突）
BENCH_SERVER = bench/bench_server
BENCH_OBJS   = $(filter-out AgentSpace/main.o,$(OBJS))

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

$(BENCH_SERVER): bench/bench_main.c $(BENCH_OBJS)
	$(CC) $(CFLAGS) -o $@ bench/bench_main.c $(BENCH_OBJS) $(LDFLAGS)

# 一键 QPS：make qps OPT="-O2 -march=native"
qps: $(BENCH_SERVER)
	bash bench/run_qps.sh

# 清理
clean:
	rm -f $(TARGET) $(LIB_TARGET) $(OBJS) $(DEPS) $(BENCH_SERVER) bench/bench_main.o bench/bench_main.d

# 运行
run: $(TARGET)
	./$(TARGET)

# 调试
debug: $(TARGET)
	gdb ./$(TARGET)

# 内存检查
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

.PHONY: clean run debug valgrind lib qps

# 头文件依赖（由 -MMD 生成；首次构建时文件不存在，用 -include 忽略）
-include $(DEPS)