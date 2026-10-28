#include "../../Include/Variate.h"


#define RECEIVER_QUEUE_ENTRY_BYTES  ((int)sizeof(Receiver_Queue_Entry))
#define RECEIVER_QUEUE_NODE_OFFSET(size) \
    ((int)(((size_t)(size) + 1 + sizeof(void*) - 1) & ~(sizeof(void*) - 1)))
#define RECEIVER_QUEUE_ALLOC_BYTES(size) \
    (RECEIVER_QUEUE_NODE_OFFSET(size) + RECEIVER_QUEUE_ENTRY_BYTES)

typedef struct Receiver_Queue_Entry{

    Worker_Queue* m_queue;
    char* char_ptr;
    void* httpNeedFree;
    uint64_t onlyId;
    off_t size_file;
    off_t offset;

    struct Receiver_Queue_Entry* pre;
    struct Receiver_Queue_Entry* next;

    int fd;
    int serial;
    int error_reason;
    int size_resp;
    int off;
    int send_fd;


}Receiver_Queue_Entry;

typedef struct Receiver_Queue{

    Receiver_Queue_Entry* head;
    Receiver_Queue_Entry* tail;

    pthread_mutex_t mutex;
    int hadNum;

}Receiver_Queue;

//这个是到时候从事件队列里取了一串事件，会先放到这里面。
//因为发送机制是，如果没有遇到要发送的事件就继续处理事件，如果遇到就先去发送，之后干别的事
//所以有可能如果取下来不先存起来，就会出现遇到要发送的去处理了，结果剩下的事件遗漏了
typedef struct Receiver_Queue_Temp{

    Receiver_Queue_Entry* head;
    Receiver_Queue_Entry* tail;
    Receiver_Queue_Entry* now;

}Receiver_Queue_Temp;

int ReceiverQueueInit(Receiver_Queue** sq);

int ReceiverQueueLen(Receiver_Queue* sq);

int ReceiverQueuePush(Receiver_Queue* sq,Worker_Queue* m_queue,int fd,uint64_t onlyId,int serial,int error_reason,int resp_size,int off,
    off_t file_size,char*char_ptr,int send_fd,off_t offset,void* httpNeedFree,int httpSize);

Receiver_Queue_Entry* ReceiverQueueTopAndPop(Receiver_Queue* sq,int* error);

int ReceiverQueueTempInit(Receiver_Queue_Temp** rq);

int ReceiverQueueTempPush(Receiver_Queue_Temp* rq,Receiver_Queue_Entry* list);

Receiver_Queue_Entry ReceiverQueueTempTopAndPop(Receiver_Queue_Temp* rq,int* error);

