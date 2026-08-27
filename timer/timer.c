#include "../include.h"



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

int timer_expend(timer* t)
{
    prior_queue_1* q2=NULL;
    prior_queue_1_init(&q2,2*(t->q->queue[0].fd));
    for(int i=0;i<t->q->end;i++)
    {
        q2->queue[i].fd=t->q->queue[i].fd;
        q2->queue[i].time=t->q->queue[i].time;
    }
    q2->elem_end=t->q->elem_end;
    free(t->q);
    t->q=q2;

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
        if(t->q->elem_end==t->q->end)
        {
            timer_expend(t);
        }
        time_t now=time(NULL);
        int e1 = prior_queue_1_insert(t->q,fd,now);
        if(e1!=1)
        {
            return -1;
        }
    }

    return 1;
}

int timer_overtime(timer* t,int overtime,worker* w)//超时时间
{
    if(t->q->elem_end==1)
    {
        return 1;
    }
    timer_entry t1=prior_queue_1_top(t->q);
    int fd=t1.fd;
    time_t now=time(NULL);

    while(t->q->queue[0].fd>0&&now-t1.time>overtime)
    {
        int e1=fd_close(w,t->q->queue[1].fd,408);
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
    }

    return 1;
}

int timer_free(timer* t,int fd)
{
    if(t->q->elem_end==1)
    {
        return 1;
    }
    int i=1;
    int can=0;
    for(;i<t->q->elem_end;i++)
    {
        if(t->q->queue[i].fd==fd)
        {
            can=1;
            break;
        }
    }

    if(can!=1)
    {
        return 1;
    }
    else
    {
        t->q->queue[i].fd=t->q->queue[t->q->elem_end-1].fd;
        t->q->queue[i].time=t->q->queue[t->q->elem_end-1].time;
        t->q->elem_end--; 
        t->q->queue[0].fd--;
        prior_queue_1_down(t->q,i);
    }

    return 1;

}
