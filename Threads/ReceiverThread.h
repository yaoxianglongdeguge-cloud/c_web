
#include "../Include/Variate.h"

#define TEMP_PACK_MAX 1024

typedef struct Temp_Pack_Entry{
    int fd;
    char* h;
    int h_size;
    int error_reason;
    int serial;
}Temp_Pack_Entry;

//任务队列满时，把请求先放进这个环形队列（原来是单槽位，会被后来的覆盖 ⇒ 整条连接丢响应）
typedef struct Temp_Pack{
    int head;
    int tail;
    int count;
    Temp_Pack_Entry e[TEMP_PACK_MAX];
}Temp_Pack;

typedef struct Receiver_Thread{
   
    int id;
    int epfd;
    int wakeup_fd;   //eventfd：业务线程把响应排入队列后立刻唤醒收发线程去发
    int notified;    //1=已经写过 eventfd（避免每个响应都 write 一次）


    _Atomic int isFull;


    Fd_Table* fd_table;
    Memory_Pool* store_area;
    Memory_Pool* http_pool;
    Timer* my_Timer;//断连计时器
    
    Receiver_Queue* Thing_queue;//接收已经准备好的要发的包的事件

    Task_Queue* taskQueue;//这个接待线程私有的任务队列（同组的工作线程/替补工作线程都从这里取）

    Temp_Pack* tempPack;
    Thread_Pool* threadPool;


}Receiver_Thread;



int ReceiverThreadInit(Receiver_Thread** w,int Listen_fd,int id,int workersNum);

int ReceiverToWorker(Receiver_Thread* w,int fd,char* h,int h_size,int error_reason,int serial);

int ReveiverThreadMain(Receiver_Thread* w,int Listen_fd,int time,int ed_store_blocknum);

int ReceiverNotify(Receiver_Thread* w);//【延迟修复】唤醒收发线程（业务线程在投递响应后调用）