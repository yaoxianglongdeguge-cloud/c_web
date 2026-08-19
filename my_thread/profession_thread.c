#include "profession_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../my_lock/my_rwlock_t.h"
#include "worker_thread.h"
#include "../server/global_resource.h"
#include "../memory_pool/memory_pool.h"
#include "../timer/timer.h"
#include "../http_analysis/http_analysis.h"
#include "../http_analysis/http_back_order.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../send_tool/send_tool.h"
#include "../send_tool/send_thing_queue.h"
#include "../connect_config/send_tool_arr_config.h"
#include "../data_struct/Task_queue.h"

int deal_task(int * e)
{
    *e=200;
    return 1;
}

int pack_task(char** c,Response)
{
    *c="HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, World!";

    int len = strlen(*c);

    return len;
}

int deal_and_pack()
{
    int e0=0;
    Task_Entry t;
    

        sem_wait(&sem_task_queue_notempty);//本来push里面也没几个操作而且几乎都要直接操作队列，所以放在这里就可以
        pthread_mutex_lock(&mutex_task);
        
        t=Task_queue_top_and_pop(Task_Queue,&e0);
        
        pthread_mutex_unlock(&mutex_task);
        sem_post(&sem_task_queue_notfull);
        
        Request Req=t.http;
        int Error_reason=t.error_reason;
        int Fd=t.fd;
        int Serial=t.serial;
        worker* W=t.w;
        
        
        int size=0;
        char* C;
        Response Rsp;
        if(Error_reason==200)
        {        
            //处理业务任务
            deal_task(&Error_reason);
            size=pack_task(&C,Rsp);
            
        }
        
        //把打包好的返回文本在内存池里填上，或者如果是错误包也指向对应错误包
        
       
    }
        
        
        
        
        