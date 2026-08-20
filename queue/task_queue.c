#include "task_queue.h"
#include <stdio.h>
#include <stdlib.h>



#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_analysis.h"



int Task_queue_init(Task_queue** sq,int blocknum)
{
    *sq=NULL;
    *sq=(Task_queue*)malloc(sizeof(Task_queue));
    if(*sq==NULL)
    {
        return -1;
    }

    (*sq)->queue=(Task_Entry*)malloc(sizeof(Task_Entry)*blocknum);
    (*sq)->begin=0;
    (*sq)->end=(*sq)->begin+blocknum;
    (*sq)->ptr_in=(*sq)->begin;
    (*sq)->ptr_out=(*sq)->begin;
    (*sq)->num=0;
    (*sq)->blocknum=blocknum;

    sem_init(&((*sq)->sem_task_queue_notfull),0,blocknum);
    pthread_mutex_init(&((*sq)->mutex_task),NULL);
    sem_init(&((*sq)->sem_task_queue_notempty),0,0);


    return 1;


}

int Task_queue_push(Task_queue* sq,worker* w,int fd,int serial,int error_reason,Http_analysis_1* h)
{
    sem_wait(&(sq->sem_task_queue_notfull));
    pthread_mutex_lock(&(sq->mutex_task));

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
    pthread_mutex_unlock(&(sq->mutex_task));
    sem_post(&(sq->sem_task_queue_notempty));

    return 1;
}

Task_Entry Task_queue_top_and_pop(Task_queue* sq,int* error)
{
    sem_wait(&(sq->sem_task_queue_notempty));
    pthread_mutex_lock(&(sq->mutex_task));
    *error=0;
    Task_Entry s;
    s.fd=-1;
    s.serial=-1;
    s.http=NULL;
    s.error_reason=-1;
    s.w=NULL;


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

    pthread_mutex_unlock(&(sq->mutex_task));
    sem_post(&(sq->sem_task_queue_notfull));
    return s;
}






