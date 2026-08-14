#include "global_resource.h"

#include"../data_struct/Task_queue.c"
#include <semaphore.h> 
#include <pthread.h> 

Task_queue* Task_Queue;

sem_t sem_task_queue_notfull;//任务队列信号量，用来指示队列还有多少个收发线程可进入
pthread_mutex_t mutex_task = PTHREAD_MUTEX_INITIALIZER;//线程进入之后依然要用锁保护
sem_t sem_task_queue_notempty;//任务队列信号量，用来指示队列还有多少个业务线程可进入


int global_resource_init(int Task_Queue_size)
{
    Task_queue_init(&Task_Queue,Task_Queue_size);
    sem(sem_task_queue_notfull,0,Task_Queue_size);
    sem(sem_task_queue_notempty,0,Task_Queue_size);



}



