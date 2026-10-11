#include "../variate.h"

typedef struct timer{

    Prior_Queue_Timer* q;

}timer;

int TimerInit(timer** t,int num);

int TimerAlloc(timer* t,int fd,worker* w);//分配给连接计时器并重置，如果已经有了那么直接找出并重置

int TimerReset(timer* t,int fd,worker* w);

int TimerOvertime(timer* t,int overtime,worker* w);//超时时间

int TimerFree(timer* t,int fd);