#include "../Include/Variate.h"

#define TASK_QUEUE_NUM 100

#define ONE_TASK_TIME 4
#define RECEIVER_GROUP_NUM_DEFAULT 1
#define RECEIVER_GROUP_NUM_MAX     32
#define PER_RECEIVER_WORKER 4
#define WORKER_THREAD_NUM_DEFAULT 2
#define WORKER_THREAD_NUM_MAX     16

// epoll 检查粒度：超时想比它更小时，需要把这个 tick 一起调小，或改用 timerfd
#ifndef CWEB_EPOLL_TICK_MS
#define CWEB_EPOLL_TICK_MS 2
#endif


int GlobalResourceInit(RouterFunc Handler);
