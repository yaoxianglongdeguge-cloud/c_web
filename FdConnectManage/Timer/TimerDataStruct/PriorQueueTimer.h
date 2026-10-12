#include "../../../variate.h"

typedef struct Timer_Entry 
{
    int fd;
    time_t time;

}Timer_Entry;

typedef struct Prior_Queue_Timer{

    int end;//整个有效内存后面一位
    int elem_end;//最后一个元素后面一位
    Timer_Entry* queue;//0号为可以存储节点个数

}Prior_Queue_Timer;//小顶堆


int PriorQueueTimerInit(Prior_Queue_Timer** p,int num);

int PriorQueueTimerInsert(Prior_Queue_Timer* p,int fd,time_t time);

int PriorQueueTimerPop(Prior_Queue_Timer* p);

Timer_Entry PriorQueueTimerTop(Prior_Queue_Timer* p);

int PriorQueueTimerDown(Prior_Queue_Timer* p,int t);
