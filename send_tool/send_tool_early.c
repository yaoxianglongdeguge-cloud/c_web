#include "send_tool_early.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include"../data_struct/prior_queue_1.h"

int send_tool_early_init(Send_tool_early* s,int num)
{
    prior_queue_1_init(&((*s)->p),num);

    return 1;
}

int send_tool_early_insert(Send_tool_early* s,int fd)
{
    for(int i=1;i<s->p->end;i++)
    {
        if(fd_site==s->p->queue[i].fd)
        {
            s->p->queue[i].time=time(NULL);
            return 1;
        }
    }

    prior_queue_1_insert(s->p,fd,time(NULL));

    return 1;


}

int send_tool_early_top(Send_tool_early* s)
{
    if(s->p->queue[0].fd==0)
    {
        return -2;
    }
    timer_entry a= prior_queue_1_top(s->p);
    return a.fd;
}

int send_tool_early_pop(Send_tool_early* s,int fd_site)
{
    int which=0;
     for(int i=1;i<s->p->end;i++)
    {
        if(fd_site==s->p->queue[i].fd)
        {
            which=1;
        }
    }

    if(which==0)
    {
        return 1;
    }

    s->p->queue[which].time=9223372036854775800;
    prior_queue_1_down(s->p,which);
    s->p->queue[0].fd--;
    s->p->elem_end--;

    return 1;

}