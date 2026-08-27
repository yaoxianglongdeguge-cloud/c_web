#include "../include.h"

Task_queue* Task_Queue;

route_1* Router;



int global_resource_init(int Task_Queue_blocknum)
{
    Router=(route_1*)malloc(sizeof(route_1));
    route_1_init();

    Task_queue_init(&Task_Queue,Task_Queue_blocknum);

}





