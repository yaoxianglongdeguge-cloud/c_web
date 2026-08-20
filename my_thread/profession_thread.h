#include <pthread.h>
#include <semaphore.h>

typedef struct Memory_Queue Memory_Queue;
typedef struct Memory_Pool Memory_Pool;

typedef struct profession{

    int id;

    Memory_Pool* txt_pool;
    Memory_Queue* memory_queue;//用来接收对方要释放内存的事件

}profession;

int profession_init(profession** profes,int id);
int deal_and_pack(profession* profes);