#include "profession_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../server/global_resource.c"
#include "../memory_pool/memory_pool.h"
#include "../timer/timer.h"
#include "../http_analysis/http_analysis.h"
#include "../http_analysis/http_back_order.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../send_tool/send_tool.h"
#include "../send_tool/send_thing_queue.h"
#include "send_tool_arr_config.h"


int deal_and_pack()
{
    int e0=0;
    Task_Entry t;
    sem_wait(&sem_task_queue_notfull);//本来push里面也没几个操作而且几乎都要直接操作队列，所以放在这里就可以
    pthread_mutex_lock(&mutex_task);

    t=Task_queue_top_and_pop(Task_Queue,&e0);

    pthread_mutex_unlock(&mutex_task);
    sem_post(&sem_task_queue_notempty);

    Request Req=t.http;
    int Error_reason=t.error_reason;
    int Fd=t.fd;
    int Serial=t.serial;
    worker* W=t.w;

    Response Rsp;

    deal_task();

 
    char* C;
    int size=pack_task(&C,Rsp);
    
    
    char** back_pack=Memory_pool_alloc(W->send_pool,size);

    memcpy(back_pack,C,size);





    
}


