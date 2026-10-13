#include "../variate.h"

typedef struct Receiver_Thread{
   
    int id;
    int epfd;

    Fd_Table* fd_table;
    Memory_Pool* store_area;
    Memory_Pool* http_pool;
    Timer* my_Timer;//断连计时器
    
    Receiver_Queue* Thing_queue;//接收已经准备好的要发的包的事件



}Receiver_Thread;


int ReceiverThreadInit(Receiver_Thread** w,int Listen_fd,int id);

int ReceiverToWorker(Receiver_Thread* w,int fd,char* h,int h_size,int error_reason,int serial);

int ReveiverThreadMain(Receiver_Thread* w,int Listen_fd,int time,int ed_store_blocknum);