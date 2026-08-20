#include "profession_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "worker_thread.h"
#include "../server/global_resource.h"
#include "../memory_pool/memory_pool.h"
#include "../timer/timer.h"
#include "../http_analysis/http_analysis.h"
#include "../send_tool/send_tool.h"
#include "../queue/send_thing_queue.h"
#include "../queue/task_queue.h"
#include "../queue/memory_queue.h"

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
    
    t=Task_queue_top_and_pop(Task_Queue,&e0);
        
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
        
        
        
        
        