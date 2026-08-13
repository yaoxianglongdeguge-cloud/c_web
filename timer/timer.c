#include "timer.h"
#include <stdlib.h>
#include <time.h>
#include "../data_struct/prior_queue_1.h"
#include "../my_thread/worker_thread.h"
#include "../connect_config/connect_manage.h"



int timer_init(timer** t,int num)
{
    *t=NULL;
    *t=(timer*)malloc(sizeof(timer));
    if(*t==NULL)
    {
        return -1;
    }

    int e0=prior_queue_1_init(&((*t)->q),num);
    if(e0!=1)
    {
        return -1;
    }

    return 1;
}

int timer_alloc_and_reset(timer* t,int fd,worker* w)//分配给连接计时器并重置，如果已经有了那么直接找出并重置
{
    int which=1;
    int f=0;

    while(which>=1&&which<t->q->elem_end)
    {
        if(t->q->queue[which].fd==fd)
        {
            f=1;
            break;
        }
        which++;
    }

    if(f==1)
    {
        t->q->queue[which].time=time(NULL);
        int e1=prior_queue_1_down(t->q,which);
        if(e1!=1)
        {
            return -1;
        }
    }
    else if(f==0)
    {
        time_t now=time(NULL);
        int e1 = prior_queue_1_insert(t->q,fd,time);
        if(e1!=1)
        {
            return -1;
        }
        int e2=fd_connect(w,fd);
        if(e2!=1)
        {
            return -1;
        }
    }

    return 1;
}

int timer_overtime(timer* t,int overtime,worker* w)//超时时间
{
    timer_entry t1=prior_queue_1_top(t->q);
    int fd=t1.fd;
    time_t time=t1.time;

    while(t->q->queue[0].fd>0&&time-(t->q->queue[1].time)>overtime)
    {
        int e1=fd_close(w,t->q->queue[1].fd);
        if(e1!=1)
        {
            return -1;
        }
        int e0=prior_queue_1_pop(t->q);
        if(e0!=1)
        {
            return -1;
        }
        t1=prior_queue_1_top(t->q);
        fd=t1.fd;
        time=t1.time;
    }

    return 1;
}
