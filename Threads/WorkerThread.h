#include "../Include/Variate.h"

typedef struct Worker_Thread{

    int id;
    _Atomic int isMain;
    _Atomic int isOut;//1为运行，0为休眠
    _Atomic pid_t tid;
    pthread_cond_t workerCond; 
    pthread_mutex_t workerMutex;

    _Atomic long long taskStart;
    _Atomic long long taskEnd;


    Task_Queue* taskQueue;//这个业务线程对应的任务队列（同组接待线程私有）

    Memory_Pool* txt_pool;
    Memory_Pool* http_pool;//http解析包
    Worker_Queue* memory_queue;//用来接收对方要释放内存的事件

}Worker_Thread;

int WorkerThreadInit(Worker_Thread** profes,int id);
int WorkerMain(Worker_Thread* profes);