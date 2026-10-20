#include "../Include/Variate.h"

typedef struct Worker_Thread{

    int id;

    Task_Queue* taskQueue;//这个业务线程对应的任务队列（同组接待线程私有）

    Memory_Pool* txt_pool;
    Memory_Pool* http_pool;//http解析包
    Worker_Queue* memory_queue;//用来接收对方要释放内存的事件

}Worker_Thread;

int WorkerThreadInit(Worker_Thread** profes,int id);
int WorkerMain(Worker_Thread* profes);