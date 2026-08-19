#include "global_resource.h"
#include"../data_struct/Task_queue.h"

Task_queue* Task_Queue;


int global_resource_init(int Task_Queue_blocknum)
{
    Task_queue_init(&Task_Queue,Task_Queue_blocknum);

}



