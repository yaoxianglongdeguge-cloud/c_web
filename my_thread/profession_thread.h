#include <pthread.h>
#include <semaphore.h>
#include "../Task_queue/memory_queue.h"

typedef struct profession{

    int id;

    Memory_Pool* txt_pool;
    Memory_Queue* memory_queue;//用来接收对方要释放内存的事件

}profession;


int deal_and_pack();