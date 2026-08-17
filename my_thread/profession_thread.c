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
    "Content-Type: application/json\r\n"
    "Content-Length: 27\r\n"
    "\r\n"
    "{\"name\":\"john\",\"age\":20}";

    int len = strlen(*c);

    return len;
}

int deal_and_pack()
{
    int e0=0;
    Task_Entry t;
    while(1)
    {

        
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
        
        int which=-2;  
        my_lock_rdlock(&(W->rwlock_table));
        which=send_tool_arr_fdget(W,Fd);
        
        if(which==-2)
        {        
            my_lock_unlock(&(W->rwlock_table));
            return 0; 
        }
        
        char* back_pack=NULL;
        if(Error_reason==200)
        {
            
            pthread_mutex_lock(&(W->mutex_pool));
            back_pack=Memory_pool_alloc(W->send_pool,size);
            pthread_mutex_unlock(&(W->mutex_pool));
            
            memcpy(back_pack,C,size);//这里虽然还是要在共享内存池写，但只会在他自己的这块内存写，不会影响到别的，而且他不释放，别的拿不到
        }
        // else
        //{
            //int e5=Error_reason_ptr(&back_pack,Error_reason);
            // }
            my_lock_unlock(&(W->rwlock_table));
            
            
            //把http请求的内存回收，业务函数用完了
            Memory_pool_free(W->http_pool,t.http,t.http->end);
            
            
            my_lock_rdlock(&(W->rwlock_table));
            which=send_tool_arr_fdget(W,Fd);
            
            if(which==-2)
            {
                if(Error_reason==200)
                {
                    
                    Memory_pool_free(W->send_pool,back_pack,back_pack+size);//这是连接已经断开，资源已经回收，但已经在返回池申请内存的
                    
                }
                my_lock_unlock(&(W->rwlock_table));
                return 0; 
            }
            
            
            
            //把返回包指针放入指针池
            sem_wait(&(W->send_tool_table->table[which].sem));
            pthread_mutex_lock(&(W->send_tool_table->table[which].mutex));
            if(Error_reason!=200)
            {
                int e4=send_tool_insert(W->send_tool_arr[which],Serial,back_pack,-Error_reason);
            }
            else
            {
                int e4=send_tool_insert(W->send_tool_arr[which],Serial,back_pack,size);
            }
            pthread_mutex_unlock(&(W->send_tool_table->table[which].mutex));
            
            my_lock_unlock(&(W->rwlock_table));
            
            
            
            my_lock_rdlock(&(W->rwlock_table));
            which=send_tool_arr_fdget(W,Fd);
            
            if(which==-2)
            {
                if(Error_reason==200)
                {
                    
                    Memory_pool_free(W->send_pool,back_pack,back_pack+size);
                    
                }
                my_lock_unlock(&(W->rwlock_table));
                return 0; 
            }
            
            
            //把返回包事件放入收发线程的事件队列
            sem_wait(&(W->sem_thing_queue_notfull));
            pthread_mutex_lock(&(W->mutex_thing));
            int e5 = Send_thing_queue_push(W->send_thing_queue,Fd,Serial);
            pthread_mutex_unlock(&(W->mutex_thing));
            
            my_lock_unlock(&(W->rwlock_table));
            
            
            
            
        }
    }
        
        
        
        
        