#include "include_standard.h"


typedef struct Router Router;

typedef struct worker worker; 
typedef struct Worker_Thread Worker_Thread;

typedef struct Fd_Entry Fd_Entry;
typedef struct Fd_Table Fd_Table;

typedef struct Memory_Queue_Entry Memory_Queue_Entry;
typedef struct Memory_Queue Memory_Queue;
typedef struct Task_Entry Task_Entry;
typedef struct Task_queue Task_queue;
typedef struct Send_tq_Entry Send_tq_Entry;
typedef struct Send_thing_queue Send_thing_queue;

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

extern Router* Router;

extern Task_queue* Task_Queue;

extern sem_t sem_task_queue_notfull;

extern pthread_mutex_t mutex_task;

extern sem_t sem_task_queue_notempty;


typedef struct Response_Header_Entry Response_Header_Entry;
typedef struct Response Response;
typedef struct Request Request;
typedef void (*Handler)(Request *, Response *);//路由后处理函数类型
typedef void (*HandlerFunc)();