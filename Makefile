# 编译器 / 归档工具
CC = gcc
AR = ar

# 编译选项
#   -I.      让 Include/Include.h 里的 ../ToOut/foo.h、../Config/CWebConfig.h 等相对包含生效
#   -MMD -MP 为每个 .o 生成 .d 依赖，改头文件后 make 会自动重编相关 .o
#   OPT      调试用 -O0；发布用 make OPT="-O2 -march=native"
OPT ?= -O0
CFLAGS = -g $(OPT) -Wall -I. -MMD -MP

# 链接选项
LDFLAGS = -lpthread

# 产物
TARGET     = main
LIB_TARGET = libcweb.a

# 仓库本身不含 main()：默认只构建静态库
.DEFAULT_GOAL := lib

# 自动查找所有 .c 文件（目录最深四层，如 FdConnectManage/Timer/TimerDataStruct/）
SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)
# 排除测试目录，避免测试自带的 main() 混进静态库
SRCS := $(filter-out test/%,$(SRCS))
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

# 静态库（先删旧包，避免已删除源文件对应的 .o 残留）
$(LIB_TARGET): $(OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

lib: $(LIB_TARGET)

# 可执行文件：需要仓库根存在含 main() 的 .c（如 app.c），用 make main 构建
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译 .c 为 .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(LIB_TARGET) $(OBJS) $(DEPS)

run: $(TARGET)
	./$(TARGET)

debug: $(TARGET)
	gdb ./$(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

.PHONY: lib clean run debug valgrind

# 头文件依赖（由 -MMD 生成；首次构建时文件不存在，用 -include 忽略）
-include $(DEPS)
