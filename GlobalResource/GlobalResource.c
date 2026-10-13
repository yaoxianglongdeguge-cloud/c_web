#include "../Include/Include.h"

Task_Queue* taskQueue;

Router* router;

int GlobalResourceInit(int Task_Queue_blocknum,RouterFunc Handler)
{
    router=(Router*)malloc(sizeof(Router));
    RouterInit();
    Handler();

    TasKQueueInit(&taskQueue,Task_Queue_blocknum);

}





