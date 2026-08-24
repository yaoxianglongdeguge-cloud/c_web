#include "../variate.h"

typedef struct Task_Entry{

    worker* w;
    int fd;
    int serial;
    int error_reason;
    char* http;
    int h_size;

}Task_Entry;

typedef struct Task_queue{

    Task_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int blocknum;//节点总个数

    sem_t sem_task_queue_notfull;//任务队列信号量，用来指示队列还有多少个收发线程可进入
    pthread_mutex_t mutex_task;//线程进入之后依然要用锁保护
    sem_t sem_task_queue_notempty;//任务队列信号量，用来指示队列还有多少个业务线程可进入

}Task_queue;


int Task_queue_init(Task_queue** sq,int size);

int Task_queue_push(Task_queue* sq,worker* w,int fd,int serial,int error_reason,char* h,int h_size);

Task_Entry Task_queue_top_and_pop(Task_queue* sq,int* error);
