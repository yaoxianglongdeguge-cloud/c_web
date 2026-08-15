#include "send_main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../my_thread/worker_thread.h"
#include "../timer/timer.h"
#include "send_tool.h"
#include "send_thing_queue.h"
#include "send_tool_early.h"
#include "../connect_config/send_tool_arr_config.h"
#include "../http_ananlysis/http_back_order.h"

int send_main(worker* w)
{
    int e0=0;
    Send_tq_Entry s;
    int which=-2;
    int next=-2;
    int next_ptr=-2;

    pthread_mutex_lock(&(w->mutex_thing));
    while(which==-2||w->send_tool_arr[which]->store[next_ptr].use==0)
    {
    s = Send_thing_queue_top_and_pop(&(w->send_thing_queue),&e0);
    if(e0==1)
    {
        my_rwlock_rdlock(&(W->rwlock_table));
        which=send_tool_arr_fdget(w,s.fd);
        my_rwlock_unlock(&(W->rwlock_table));
        if(which>=0)
        {
            next=http_back_order_get(w->http_order,s.fd,2);
            next_ptr=next/w->send_tool_arr[which]->blocknum;
        }
        
    }
    else if(e0==0)
    {
        break;
    }

    }
    if(e0==0)
    {
        return 1;
    }

    sem_post(&(w->sem_thing_queue_notfull));
    pthread_mutex_unlock(&(w->mutex_thing));


    int len=0;
    if(w->send_tool_arr[which]->store[next_ptr].error_reason<0)
    {




    }
    else
    {
        len=w->send_tool_arr[which]->store[next_ptr].error_reason;
    }


    while(w->send_tool_arr[which]->store[next_ptr].use==1)
    {
        write(w->send_tool_arr[which]->store[next_ptr]., response + written, len);
        http_back_order_add(w->http_order,s.fd,2);
        next_ptr=next_ptr+1/w->send_tool_arr[which]->blocknum;
    }

    final_ptr=http_back_order_get(w->http_order,s.fd,3);
    if(next_ptr>final_ptr)
    {
        my_rwlock_wrlock(&(w->rwlock_table));
        send_tool_arr_fdfree(w,s.fd);
        my_rwlock_unlock(&(w->rwlock_table));
        send_tool_early_pop(w->send_early,which);
    }

    timer_alloc_and_reset(w->my_timer,s.fd,w);







    //给最开始分配的连接也释放一下
    int early_fd=send_tool_early_top(w->send_early);
    if(early_fd==-2)
    {
        return 1;
    }

    my_rwlock_rdlock(&(W->rwlock_table));
    int which2=send_tool_arr_fdget(w,s.fd);
    my_rwlock_unlock(&(W->rwlock_table));

    if(which2<0)
    {
        return 0;
    }
    int next2=http_back_order_get(w->http_order,early_fd,2);
    int next_ptr2=next2/w->send_tool_arr[which2]->blocknum;

    int len2=0;
    if(w->send_tool_arr[which2]->store[next_ptr2].error_reason<0)
    {




    }
    else
    {
        len2=w->send_tool_arr[which2]->store[next_ptr2].error_reason;
    }


    while(w->send_tool_arr[which2]->store[next_ptr2].use==1)
    {
        write(w->send_tool_arr[which2]->store[next_ptr2]., response + written, len);
        next_ptr2=next_ptr2+1/w->send_tool_arr[which2]->blocknum;
    }

    int final_ptr2=http_back_order_get(w->http_order,early_fd,3);

    if(next_ptr2>final_ptr2)
    {
        my_rwlock_wrlock(&(w->rwlock_table));
        send_tool_arr_fdfree(w,early_fd);
        my_rwlock_unlock(&(w->rwlock_table));
    }
    timer_alloc_and_reset(w->my_timer,early_fd,w);

    return 1;

}

