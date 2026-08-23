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

int profession_init(profession** profes,int id)
{
    *profes=(profession*)malloc(sizeof(profession));
    (*profes)->id=id;
    Memory_Queue_init(&((*profes)->memory_queue),40);
    Memory_Pool_init(&((*profes)->txt_pool),6,6,50,5);
    return 1;
}

int deal_and_pack(profession* profes)
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
        
            //处理业务任务
    deal_task(&Error_reason);
    size=pack_task(&C,Rsp);

    Send_thing_queue_push(W->Thing_queue,profes->memory_queue,Fd,Serial,Error_reason,size,C,t.http);

    
    int e1=1;
    while(e1!=0)
    {
        Memory_Queue_Entry m;

        m=Memory_Queue_top_and_pop(profes->memory_queue,&e1);
        /*if(e1==1)
        {
            Memory_Pool_free(profes->txt_pool,m.char_ptr,m.size);
        }*/
    }
    
        
        //把打包好的返回文本在内存池里填上，或者如果是错误包也指向对应错误包        
       
    }
        
        
        
        
        