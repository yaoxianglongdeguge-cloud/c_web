#include "../../Include/Variate.h"

typedef struct Worker_Queue_Entry{

    int size;
    char* char_ptr;

}Worker_Queue_Entry;

typedef struct Worker_Queue{

    Worker_Queue_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int blocknum;//节点总个数

    sem_t queue_notfull;//返回包事件的队列的信号量，用来指示队列还有多少个业务线程可以进入
    pthread_mutex_t mutex;

}Worker_Queue;


int WorkerQueueInit(Worker_Queue** sq,int blocknum);

int WorkerQueuePush(Worker_Queue*sq,int size,char*char_ptr);

Worker_Queue_Entry WorkerQueueTopAndPop(Worker_Queue* sq,int* error);
