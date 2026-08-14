#include "Task_queue.h"
#include <stdio.h>
#include <stdlib.h>



#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_analysis.h"



int Task_queue_init(Task_queue** sq,int size)
{
    *sq=NULL;
    *sq=(Task_queue*)malloc(sizeof(Task_queue));
    if(*sq==NULL)
    {
        return -1;
    }

    (*sq)->queue=(Task_Entry*)malloc(sizeof(Task_Entry)*size);
    (*sq)->begin=0;
    (*sq)->end=(*sq)->begin+size;
    (*sq)->ptr_in=(*sq)->begin;
    (*sq)->ptr_out=(*sq)->begin;
    (*sq)->num=0;
    (*sq)->size=size;

    return 1;


}

int Task_queue_push(Task_queue* sq,worker* w,int fd,int serial,int error_reason,Http_analysis_1* h)
{
    if(sq->num==sq->size)
    {
        return 0;
    }

    sq->queue[sq->ptr_in].w=w;
    sq->queue[sq->ptr_in].error_reason=error_reason;
    sq->queue[sq->ptr_in].http=h;
    sq->queue[sq->ptr_in].fd=fd;
    sq->queue[sq->ptr_in].serial=serial;

    if(sq->ptr_in+1==sq->end)
    {
        sq->ptr_in=sq->begin;
    }
    else
    {
        sq->ptr_in++;
    }

    sq->num++;

    return 1;
}

Task_Entry Send_thing_queue_top_and_pop(Task_queue* sq,int* error)
{
    *error=0;
    Task_Entry s;
    s.fd=-1;
    s.serial=-1;
    s.http=NULL;
    s.error_reason=-1;
    s.w=NULL;

    if(sq->num==0)
    {
        *error=0;
        return s;
    }

    s.fd=sq->queue[sq->ptr_out].fd;
    s.serial=sq->queue[sq->ptr_out].serial;
    s.http=sq->queue[sq->ptr_out].http;
    s.error_reason=sq->queue[sq->ptr_out].error_reason;
    s.w=sq->queue[sq->ptr_out].w;

    if(sq->ptr_out+1==sq->end)
    {
        sq->ptr_out=sq->begin;
    }
    else
    {
        sq->ptr_out++;
    }
    sq->num--;
    *error=1;

    return s;
}






