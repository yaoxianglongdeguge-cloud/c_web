#include "../variate.h"

typedef struct Worker_Thread{

    int id;

    Memory_Pool* txt_pool;
    Memory_Pool* http_pool;//http解析包
    Memory_Queue* memory_queue;//用来接收对方要释放内存的事件

}Worker_Thread;

int WorkerThreadInit(Worker_Thread** profes,int id);
int WorkerMain(Worker_Thread* profes);