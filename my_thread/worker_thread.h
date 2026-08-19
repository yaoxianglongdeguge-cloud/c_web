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

    Memory_Pool* store_area;
    Memory_Pool* http_pool;
    Send_thing_queue* Thing_queue;//接收已经准备好的要发的包的事件
    timer* my_timer;//断连计时器
    sem_t sem_thing_queue_notfull;//返回包事件的队列的信号量，用来指示队列还有多少个业务线程可以进入
    pthread_mutex_t mutex_thing;


} worker;


int worker_init(worker** w,int Listen_fd,int id);

int worker_to_profession(worker* w,int fd,Http_analysis_1* h,int error_reason,int serial);

int receive_and_send_main(worker* w,int Listen_fd,int time);