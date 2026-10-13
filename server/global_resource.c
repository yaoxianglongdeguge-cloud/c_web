#include "../include.h"

Task_queue* Task_Queue;

Router* Router;



int global_resource_init(int Task_Queue_blocknum)
{
    Router=(Router*)malloc(sizeof(Router));
    RouterInit();

    Task_queue_init(&Task_Queue,Task_Queue_blocknum);

}





