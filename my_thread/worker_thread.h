#include <pthread.h>
#include <semaphore.h>
#include "../my_lock/my_rwlock_t.h"

typedef struct my_rwlock_t my_rwlock_t;
typedef struct http_ed_store http_ed_store;
typedef struct memory_pool memory_pool;
typedef struct Send_tool Send_tool;
typedef struct Send_thing_queue Send_thing_queue;
typedef struct timer timer;
typedef struct Store_table Store_table;
typedef struct Http_back_order Http_back_order;
typedef struct Send_table Send_table;
typedef struct Send_tool_early Send_tool_early;
typedef struct Http_analysis_1 Http_analysis_1;

typedef struct worker{
   
    int id;
    int epfd;

    
    http_ed_store** http_ed_store_arr;//解析前暂存http包.
    Store_table* store_pool_table;//http包解析前暂存池管理表
    memory_pool* http_pool;//解析后的http包的暂存处
    Http_back_order* http_order;//记录每个连接下一个处理的包的序号，方便后面标记顺序返回

    Send_tool** send_tool_arr;//用来管理顺序指针循环队列，下一个空位，每个连接下一个要接收的包,还有每个连接最后要接收的包，方便释放指针队列
    Send_table* send_tool_table;
    memory_pool* send_pool;//要发回的包的暂存处
    Send_thing_queue* send_thing_queue;//接收已经准备好的要发的包的事件

    timer* my_timer;//断连计时器
    Send_tool_early* send_early;//用来标记每个连接的指针池的时间，并且能从中找出最开始那个


    my_rwlock_t rwlock_table;//读写指针池分配表
    pthread_mutex_t mutex_pool;//返回包字节实际储存位置的内存池的锁
    
    sem_t sem_thing_queue_notfull;//返回包事件的队列的信号量，用来指示队列还有多少个业务线程可以进入
    pthread_mutex_t mutex_thing;



} worker;


int worker_init(worker** w,int Listen_fd,int id);

int worker_to_profession(worker* w,int fd,Http_analysis_1* h,int error_reason,int serial);

int receive_and_send_main(worker* w,int Listen_fd,int time);