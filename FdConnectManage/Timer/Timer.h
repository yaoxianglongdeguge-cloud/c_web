#include "../../variate.h"

typedef struct Timer{

    Prior_Queue_Timer* q;

}Timer;

int TimerInit(Timer** t,int num);

int TimerAlloc(Timer* t,int fd,Receiver_Thread* w);//分配给连接计时器并重置，如果已经有了那么直接找出并重置

int TimerReset(Timer* t,int fd,Receiver_Thread* w);

int TimerOvertime(Timer* t,int overtime,Receiver_Thread* w);//超时时间

int TimerFree(Timer* t,int fd);