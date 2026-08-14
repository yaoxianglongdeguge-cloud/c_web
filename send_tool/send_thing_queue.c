#include "send_thing_queue.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct Send_tq_Entry{

    int fd;
    int serial;

}Send_tq_Entry;

int Send_thing_queue_init(Send_thing_queue** sq,int size)
{
    *sq=NULL;
    *sq=(Send_thing_queue*)malloc(sizeof(Send_thing_queue));
    if(*sq==NULL)
    {
        return -1;
    }

    (*sq)->queue=(Send_tq_Entry*)malloc(sizeof(Send_tq_Entry)*size);
    (*sq)->begin=0;
    (*sq)->end=(*sq)->begin+size;
    (*sq)->ptr_in=(*sq)->begin;
    (*sq)->ptr_out=(*sq)->begin;
    (*sq)->num=0;
    (*sq)->size=size;

    return 1;


}

int Send_thing_queue_push(Send_thing_queue* sq,int fd,int serial)
{
    if(sq->num==sq->size)
    {
        return 0;
    }

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

Send_tq_Entry Send_thing_queue_top_and_pop(Send_thing_queue* sq,int* error )
{
    *error=0;
    Send_tq_Entry s;
    s.fd=-1;
    s.serial=-1;

    if(sq->num==0)
    {
        *error=0;
        return s;
    }

    s.fd=sq->queue[sq->ptr_out].fd;
    s.serial=sq->queue[sq->ptr_out].serial;
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