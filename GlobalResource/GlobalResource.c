#include "../Include/Include.h"

//任务队列不再全局唯一：每个接待线程在 ReceiverThreadInit 里建自己的队列，
//这里只保存容量，供接待线程初始化时使用
int taskQueueBlocknum=100;

Router* router;

int GlobalResourceInit(int Task_Queue_blocknum,RouterFunc Handler)
{
    taskQueueBlocknum=Task_Queue_blocknum;

    router=(Router*)malloc(sizeof(Router));
    RouterInit();
    Handler();

    return 1;
}





