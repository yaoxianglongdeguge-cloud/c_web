#include <pthread.h>
#include <semaphore.h>


typedef struct Memory_Queue{

    Memory_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int blocknum;//节点总个数

    sem_t queue_notfull;//返回包事件的队列的信号量，用来指示队列还有多少个业务线程可以进入
    pthread_mutex_t mutex;

}Memory_Queue;

typedef struct Memory_Entry{

    int size;
    const char* const char_ptr;

}Memory_Entry;

int Memory_Queue_init(Memory_Queue** sq,int size);

int Memory_Queue_push(Memory_Queue* sq,int size,const char* const char_ptr);

Memory_Entry Memory_Queue_top_and_pop(Memory_Queue* sq,int* error);
