# c_web 单元测试
# 每个用例独立可执行，只链接被测模块 + 少量依赖。
# 注意（BUG-09）：根 Makefile 的 `SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)`
#   会把本目录的 src/*.c 一起编入主程序，导致根目录 `make` 失败（multiple definition of `main`）。
#   测试代码与主程序共存请用：
#     make TARGET=/tmp/cweb_build SRCS="$(find . -name '*.c' -not -path './test/*' -printf '%P\n')"
#   或在根 Makefile 加 `SRCS := $(filter-out test/%,$(SRCS))`。
# 用法： make -C test run     （内置 30s/用例超时，卡死用例会被判定为 HANG 而不是永久阻塞）

ROOT    := ..
CC      := gcc
CFLAGS  := -g -O0 -Wall -I$(ROOT)   # -I$(ROOT)：Include/Include.h 里 #include "foo.h" 要能命中仓库根的 foo.h
LDFLAGS := -lpthread
SRC     := src
BUILD   := build

#------------------------------------------ 被测源码 ------------------------------------------
MEM    := $(ROOT)/MemoryPool/MemoryPool.c
LOCK   := $(ROOT)/MyLock/my_rwlock_t.c
RHASH  := $(ROOT)/Router/RouterDataStruct/RouterHash.c
AHASH  := $(ROOT)/RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.c
REQ    := $(ROOT)/RequestAndResponse/Request.c
RSP    := $(ROOT)/RequestAndResponse/Response.c
QUEUE  := $(ROOT)/Queue/WorkerQueue/WorkerQueue.c \
          $(ROOT)/Queue/TaskQueue/TaskQueue.c \
          $(ROOT)/Queue/ReceiverQueue/ReceiverQueue.c
PARSER := $(ROOT)/RequestCome/RequestAnalysis/RequestAnalysiser.c $(AHASH)
TEMP   := $(ROOT)/FdConnectResource/RequestTempPool/RequestTempPool.c \
          $(ROOT)/FdConnectResource/RequestTempPool/StateMachine/StateMachine.c \
          $(ROOT)/RequestCome/RequestIntergrity/RequestIntergrity.c
TIMER  := $(ROOT)/FdConnectManage/Timer/Timer.c \
          $(ROOT)/FdConnectManage/Timer/TimerDataStruct/PriorQueueTimer.c
ROUTER := $(ROOT)/Router/Router.c $(RHASH)
EPOLL  := $(ROOT)/FdConnectManage/EpollOperate/EpollOperate.c

TESTS := test_memory_pool test_rwlock test_hash test_queues test_parser \
         test_response test_temp_pool test_timer test_router \
         test_integrity test_epoll test_taskqueue_race

#------------------------------------------ 用例依赖 ------------------------------------------
$(BUILD)/test_memory_pool: $(SRC)/test_memory_pool.c $(MEM)
$(BUILD)/test_rwlock:      $(SRC)/test_rwlock.c $(LOCK)
$(BUILD)/test_hash:        $(SRC)/test_hash.c $(RHASH) $(AHASH) $(REQ)
$(BUILD)/test_queues:      $(SRC)/test_queues.c $(QUEUE)
$(BUILD)/test_parser:      $(SRC)/test_parser.c $(PARSER) $(MEM)
$(BUILD)/test_response:    $(SRC)/test_response.c $(RSP) $(MEM)
$(BUILD)/test_temp_pool:   $(SRC)/test_temp_pool.c $(TEMP) $(MEM)
$(BUILD)/test_timer:       $(SRC)/test_timer.c $(TIMER)
$(BUILD)/test_router:      $(SRC)/test_router.c $(ROUTER)
$(BUILD)/test_integrity:   $(SRC)/test_integrity.c $(TEMP) $(PARSER) $(MEM)
$(BUILD)/test_epoll:       $(SRC)/test_epoll.c $(EPOLL)
# 任务队列 CAS 改造后的并发/死锁/不变式专项（多生产者多消费者压力）
$(BUILD)/test_taskqueue_race: $(SRC)/test_taskqueue_race.c $(ROOT)/Queue/TaskQueue/TaskQueue.c

$(BUILD)/%: | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

all: $(addprefix $(BUILD)/,$(TESTS))

run: all
	@fail=0; \
	for t in $(TESTS); do \
	  echo "========== $$t =========="; \
	  timeout -k 5 30 stdbuf -o0 -e0 ./$(BUILD)/$$t; rc=$$?; \
	  if [ $$rc -eq 124 ] || [ $$rc -eq 137 ]; then \
	    echo "   [HANG] $$t 超过 30s 未返回，已强制终止 → 被测函数很可能死循环（上面最后一行是卡住前的进度）"; fail=1; \
	  elif [ $$rc -ne 0 ]; then \
	    fail=1; \
	  fi; \
	done; \
	if [ $$fail -eq 0 ]; then echo "==== 全部单元测试通过 ===="; \
	else echo "==== 存在失败/卡死用例 ===="; exit 1; fi

# 网站集成测试（构建主程序 + 启动服务器 + curl 校验，含 10MB 大文件下载）
website:
	bash website_test.sh

clean:
	rm -f $(BUILD)/*

.PHONY: all run website clean
