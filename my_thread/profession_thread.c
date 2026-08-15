#include "profession_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../my_lock/my_rwlock_t.h"

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


    
    pthread_mutex_lock(&(W->mutex_pool));
    char* back_pack=Memory_pool_alloc(W->send_pool,size);
    pthread_mutex_unlock(&(W->mutex_pool));

    memcpy(back_pack,C,size);//这里虽然还是要在共享内存池写，但只会在他自己的这块内存写，不会影响到别的，而且他不释放，别的拿不到
    

    int e1=0;
    int which=-2;
    while(e1=0)
    {

        my_rwlock_rdlock(&(W->rwlock_table));
        which=send_tool_arr_fdget(W,Fd);
        if(which==-2)
        {
            my_rwlock_unlock(&(W->rwlock_table));
            my_rwlock_wrlock(&(W->rwlock_table));
            which=send_tool_arr_fdalloc(W,Fd);
            my_rwlock_unlock(&(W->rwlock_table));
            if(which>=0)
            {
                e1=1;
            }
        }
    }

    sem_wait(&(W->send_tool_table->table[which].sem));
    pthread_mutex_lock(&(W->send_tool_table->table[which].mutex));
    int e4=send_tool_insert(W->send_tool_arr[which],Serial,back_pack);
    pthread_mutex_unlock(&(W->send_tool_table->table[which].mutex));


    sem_wait(&(W->sem_thing_queue_notfull));
    pthread_mutex_lock(&(W->mutex_thing));
    int e5 = Send_thing_queue_push(&(W->send_thing_queue),Fd,Serial);
    pthread_mutex_unlock(&(W->mutex_thing));

}





