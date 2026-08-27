#include "include_standard.h"


typedef struct route_1 route_1;

typedef struct worker worker; 
typedef struct profession profession;

typedef struct Hash_Entry_3 Fd_Entry;
typedef struct Hash_map_3 Fd_Table;

typedef struct Memory_Queue_Entry Memory_Queue_Entry;
typedef struct Memory_Queue Memory_Queue;
typedef struct Task_Entry Task_Entry;
typedef struct Task_queue Task_queue;
typedef struct Send_tq_Entry Send_tq_Entry;
typedef struct Send_thing_queue Send_thing_queue;

typedef struct http_ed_store http_ed_store;
typedef struct http_state http_state;
typedef struct Http_analysis_1 Http_analysis_1;

typedef struct Send_tool_Entry Send_tool_Entry;
typedef struct Send_tool Send_tool;

typedef struct Memory_Stack Memory_Stack;
typedef struct Memory_Entry Memory_Entry;
typedef struct Memory_Pool Memory_Pool;


typedef struct timer timer;


typedef struct Entry Entry;
typedef struct Hash_map Hash_map;
typedef struct Hash2_Entry_2 Hash2_Entry_2;
typedef struct Hash_map_2 Hash_map_2;
typedef struct Hash_Entry_3 Hash_Entry_3;
typedef struct Hash_map_3 Hash_map_3;
typedef struct timer_entry timer_entry;
typedef struct prior_queue_1 prior_queue_1;

typedef struct my_rwlock_t my_rwlock_t;

extern route_1* Router;

extern Task_queue* Task_Queue;

extern sem_t sem_task_queue_notfull;

extern pthread_mutex_t mutex_task;

extern sem_t sem_task_queue_notempty;


typedef struct Response_Header_Entry Response_Header_Entry;
typedef struct Response Response;
typedef struct Request Request;
typedef void (*Handler)(Request *, Response *);//路由后处理函数类型
typedef void (*HandlerFunc)();