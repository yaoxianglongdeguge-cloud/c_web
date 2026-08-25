#include "../include.h"

Task_queue* Task_Queue;


int global_resource_init(int Task_Queue_blocknum)
{
    int my_block_sem_init();
    Task_queue_init(&Task_Queue,Task_Queue_blocknum);

}




