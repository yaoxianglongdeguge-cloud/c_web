#include "../../Include/Variate.h"

typedef struct Receiver_Queue_Entry{

    Worker_Queue* m_queue;
    int fd;
    int serial;
    int error_reason;
    char* char_ptr;
    int size_resp;
    int off;
    int send_fd;
    off_t size_file;
    off_t offset;
    void* httpNeedFree;


}Receiver_Queue_Entry;

typedef struct Receiver_Queue{

    Receiver_Queue_Entry* buf[2];
    int active;          //生产者当前写入的缓冲
    int pending;         //消费者当前摘下来处理的缓冲

    int active_num;      //active 中已写入条数（也是下一条写入下标）
    int pending_idx;     //pending 中下一条要消费的下标（消费者私有）
    int pending_num;     //本轮摘下来的总条数（消费者私有）

    int blocknum;//单个缓冲容量

    sem_t sem_thing_queue_notfull;//按“未消费事件数”计数的背压信号量
    pthread_mutex_t mutex_thing;


}Receiver_Queue;



int ReceiverQueueInit(Receiver_Queue** sq,int blocknum);

//线程安全地读队列长度（active + 尚未消费的 pending）。
//注意：pending 计数是消费者线程私有的，本函数只应由消费者线程调用。
int ReceiverQueueLen(Receiver_Queue* sq);

//多生产者安全：任意业务线程都可以并发 push。
int ReceiverQueuePush(Receiver_Queue* sq,Worker_Queue* m_queue,int fd,int serial,int error_reason,int resp_size,int off,
    off_t file_size,char*char_ptr,int send_fd,off_t offset,void* httpNeedFree);

//单消费者取一条。内部会在需要时把 active 整批摘下（一次加锁）。
//0为空（*error=0）；否则 *error=1。
Receiver_Queue_Entry ReceiverQueueTopAndPop(Receiver_Queue* sq,int* error);
