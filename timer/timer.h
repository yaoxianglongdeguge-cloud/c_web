#include "../variate.h"

typedef struct timer{

    prior_queue_1* q;

}timer;

int timer_init(timer** t,int num);

int timer_alloc_and_reset(timer* t,int fd,worker* w);//分配给连接计时器并重置，如果已经有了那么直接找出并重置

int timer_overtime(timer* t,int overtime,worker* w);//超时时间