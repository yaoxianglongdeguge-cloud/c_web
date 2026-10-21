#include "IncludeStandard.h"


typedef struct Router Router;
typedef void (*RouterFunc)();

long long now_ns(void);

typedef struct Receiver_Thread Receiver_Thread; 
typedef struct Worker_Thread Worker_Thread;
typedef struct Thread_Pool Thread_Pool;

typedef struct Fd_Entry Fd_Entry;
typedef struct Fd_Table Fd_Table;

typedef struct Worker_Queue_Entry Worker_Queue_Entry;
typedef struct Worker_Queue Worker_Queue;
typedef struct Task_Queue_Entry Task_Queue_Entry;
typedef struct Task_Queue Task_Queue;
typedef struct Receiver_Queue_Entry Receiver_Queue_Entry;
typedef struct Receiver_Queue Receiver_Queue;

typedef struct Request_Temp_Pool Request_Temp_Pool;
typedef struct StateMachine StateMachine;
typedef struct Request_Analysiser Request_Analysiser;

typedef struct Send_Pointer_Pool_Entry Send_Pointer_Pool_Entry;
typedef struct Send_Pointer_Pool Send_Pointer_Pool;

typedef struct Memory_Stack Memory_Stack;
typedef struct Memory_Entry Memory_Entry;
typedef struct Memory_Pool Memory_Pool;


typedef struct Timer Timer;


typedef struct Router_Hash_Entry Router_Hash_Entry;
typedef struct Router_Hash Router_Hash;
typedef struct Analysiser_Hash_Entry Analysiser_Hash_Entry;
typedef struct Analysiser_Hash Analysiser_Hash;
typedef struct Timer_Entry Timer_Entry;
typedef struct Prior_Queue_Timer Prior_Queue_Timer;

typedef struct my_rwlock_t my_rwlock_t;

extern Router* router;

extern int taskQueueBlocknum;//每个接待线程私有任务队列的容量（GlobalResourceInit 里设置）

//extern sem_t sem_task_queue_notfull;     // D-31：阻塞式背压已取消，本声明无定义/无引用
extern pthread_mutex_t mutex_task;
//extern sem_t sem_task_queue_notempty;    // D-31：同上


typedef struct Response_Header_Entry Response_Header_Entry;
typedef struct Response Response;
typedef struct Request Request;
typedef void (*Handler)(Request *, Response *);//路由后处理函数类型
typedef void (*HandlerFunc)();