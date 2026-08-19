#include <semaphore.h> 
#include <pthread.h> 

typedef struct Send_tq_Entry{

    int fd;
    int serial;
    int error_reason;
    int size;
    const char* const char_ptr;
    Http_analysis_1* http;

}Send_tq_Entry;

typedef struct Send_thing_queue{

    Send_tq_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int blocknum;//节点总个数

    sem_t sem_thing_queue_notfull;//返回包事件的队列的信号量，用来指示队列还有多少个业务线程可以进入
    pthread_mutex_t mutex_thing;


}Send_thing_queue;



int Send_thing_queue_init(Send_thing_queue** sq,int blocknum);

int Send_thing_queue_push(Send_thing_queue* sq,int fd,int serial,int error_reason,int size,char*char_ptr,Http_analysis_1* h)

Send_tq_Entry Send_thing_queue_top_and_pop(Send_thing_queue_queue* sq,int* error);