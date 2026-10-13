#include "../include.h"

Task_Queue* Task_Queue;

Router* Router;



int GlobalResourceInit(int Task_Queue_blocknum,HandlerFunc Handler)
{
    Router=(Router*)malloc(sizeof(Router));
    RouterInit();
    Handler();

    TasKQueueInit(&Task_Queue,Task_Queue_blocknum);

}





