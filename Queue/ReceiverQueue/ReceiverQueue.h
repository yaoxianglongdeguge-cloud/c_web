#include "../../Include/Variate.h"


#define RECEIVER_QUEUE_ENTRY_BYTES  ((int)sizeof(Receiver_Queue_Entry))
#define RECEIVER_QUEUE_NODE_OFFSET(size) \
    ((int)(((size_t)(size) + 1 + sizeof(void*) - 1) & ~(sizeof(void*) - 1)))
#define RECEIVER_QUEUE_ALLOC_BYTES(size) \
    (RECEIVER_QUEUE_NODE_OFFSET(size) + RECEIVER_QUEUE_ENTRY_BYTES)

typedef struct Receiver_Queue_Entry{

    Worker_Queue* m_queue;
    int fd;
    int serial;
    int error_reason;
    char* char_ptr;
    int size_resp;
    int off;
    int send_fd;
    off_t size_file;
    off_t offset;

    struct Receiver_Queue_Entry* pre;
    struct Receiver_Queue_Entry* next;
    void* httpNeedFree;


}Receiver_Queue_Entry;

typedef struct Receiver_Queue{

    Receiver_Queue_Entry* head;
    Receiver_Queue_Entry* tail;

    pthread_mutex_t mutex;
    int hadNum;

}Receiver_Queue;

typedef struct Receiver_Queue_Temp{

    Receiver_Queue_Entry* head;
    Receiver_Queue_Entry* tail;
    Receiver_Queue_Entry* now;

}Receiver_Queue_Temp;

int ReceiverQueueInit(Receiver_Queue** sq);

int ReceiverQueueLen(Receiver_Queue* sq);

int ReceiverQueuePush(Receiver_Queue* sq,Worker_Queue* m_queue,int fd,int serial,int error_reason,int resp_size,int off,
    off_t file_size,char*char_ptr,int send_fd,off_t offset,void* httpNeedFree,int httpSize);

Receiver_Queue_Entry* ReceiverQueueTopAndPop(Receiver_Queue* sq,int* error);

int ReceiverQueueTempInit(Receiver_Queue_Temp** rq);

int ReceiverQueueTempPush(Receiver_Queue_Temp* rq,Receiver_Queue_Entry* list);

Receiver_Queue_Entry ReceiverQueueTempTopAndPop(Receiver_Queue_Temp* rq,int* error);

