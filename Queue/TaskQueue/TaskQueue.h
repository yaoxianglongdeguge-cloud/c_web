#include "../../Include/Variate.h"

typedef struct Task_Queue_Entry{

    Receiver_Thread* w;
    int fd;
    int serial;
    int error_reason;
    char* http;
    int h_size;

}Task_Queue_Entry;

//任务队列：双缓冲（收发线程只写 active，业务线程只读 pending）
//  · push 和 pop 不再抢同一把锁：生产者锁内只往 active 尾部写一条
//  · pending 全部被认领、且没有业务线程正在拷贝时，在锁内把两块缓冲对调（O(1)）
//  · 多个业务线程用 pending_idx 原子认领下标，同一条任务只会被一个线程取走
typedef struct Task_Queue{

    Task_Queue_Entry* buf[2];
    int active;          //生产者当前写入的缓冲
    int pending;         //消费者当前处理的缓冲

    int active_num;      //active 中已写入条数（生产者私有，锁内修改）
    int pending_num;     //本轮 pending 的总条数（换缓冲时写）
    int pending_idx;     //下一条要被认领的下标（多消费者原子自增）
    int pending_reader;  //正在读 pending 的消费者数（换缓冲前必须为 0）
    int swapping;        //1=正在换缓冲，消费者先别认领

    int num;//有效节点个数（已入队、还没被认领）
    int blocknum;//节点总个数

    pthread_mutex_t mutex_task;//只保护 active 的写入和换缓冲
    sem_t sem_task_notempty;//有任务时唤醒业务线程；只 post 不 wait ⇒ 不会引入阻塞/死锁

}Task_Queue;


int TasKQueueInit(Task_Queue** sq,int size);

//非阻塞：1=入队成功，0=队列满（调用方放回 Temp_Pack，稍后重试）
int TasKQueuePush(Task_Queue* sq,Receiver_Thread* w,int fd,int serial,int error_reason,char* h,int h_size);

//非阻塞取一条：*error=1 成功，0 为空
Task_Queue_Entry TasKQueueTopAndPop(Task_Queue* sq,int* error);

