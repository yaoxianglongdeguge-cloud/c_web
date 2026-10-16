#include "../Include/Variate.h"

typedef struct Temp_Pack{
    bool had;
    int fd;
    char* h;
    int h_size;
    int error_reason;
    int serial;
}Temp_Pack;

typedef struct Receiver_Thread{
   
    int id;
    int epfd;
    int wakeup_fd;   //【延迟修复】eventfd：业务线程把响应排入队列后立刻唤醒收发线程去发

    Fd_Table* fd_table;
    Memory_Pool* store_area;
    Memory_Pool* http_pool;
    Timer* my_Timer;//断连计时器
    
    Receiver_Queue* Thing_queue;//接收已经准备好的要发的包的事件

    Temp_Pack* tempPack;



}Receiver_Thread;



int ReceiverThreadInit(Receiver_Thread** w,int Listen_fd,int id);

int ReceiverToWorker(Receiver_Thread* w,int fd,char* h,int h_size,int error_reason,int serial);

int ReveiverThreadMain(Receiver_Thread* w,int Listen_fd,int time,int ed_store_blocknum);

int ReceiverNotify(Receiver_Thread* w);//【延迟修复】唤醒收发线程（业务线程在投递响应后调用）