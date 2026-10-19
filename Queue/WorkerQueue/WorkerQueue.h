#include "../../Include/Variate.h"

//内存回收队列的节点：不单独 malloc，直接“搭便车”放在响应块尾部。
//  · NeedFree 保存响应块基址，释放时必须用它（不是节点地址）
//  · 节点放在“响应字节数 + 结尾 '\0'”之后，并向上对齐到指针宽度，
//    既不让 PackBackHead 写出的 '\0' 压到节点，也保证指针字段对齐。
#define WORKER_QUEUE_ENTRY_BYTES  ((int)sizeof(Worker_Queue_Entry))
#define WORKER_QUEUE_NODE_OFFSET(size) \
    ((int)(((size_t)(size) + 1 + sizeof(void*) - 1) & ~(sizeof(void*) - 1)))
#define WORKER_QUEUE_ALLOC_BYTES(size) \
    (WORKER_QUEUE_NODE_OFFSET(size) + WORKER_QUEUE_ENTRY_BYTES)

typedef struct Worker_Queue_Entry{

    char* NeedFree;
    struct Worker_Queue_Entry* pre;
    struct Worker_Queue_Entry*  next;

}Worker_Queue_Entry;

typedef struct Worker_Queue{

    Worker_Queue_Entry* head;
    Worker_Queue_Entry* tail;

    pthread_mutex_t mutex;
    int hadNum;

}Worker_Queue;


int WorkerQueueInit(Worker_Queue** sq);

int WorkerQueuePush(Worker_Queue*sq,int size,char*char_ptr);

//一次性摘下整条队列，返回单链表头（用 next 串起来，最后一个 next=NULL）。
//队列为空返回 NULL 且 *error=0；否则 *error=1。
//调用者负责遍历并用节点的 NeedFree 归还内存块。
Worker_Queue_Entry* WorkerQueueTopAndPop(Worker_Queue* sq,int* error);
