#include"send_tool.h"
#include <stdio.h>
#include <stdlib.h>

int send_tool_init(Send_tool_Entry* s,int blocknum)
{
    s=(Send_tool*)malloc(sizeof(Send_tool));
    for(int i=0;i<blocknum;i++)
    {
        (*s)->store[i].ptr=NULL;
        (*s)->store[i].use=0;
    }
    (*s)->blocknum=blocknum;

    return 1;
}

int send_tool_insert(Send_tool* s,int serial,char* ch,int error_reason)
{
    int block=s->blocknum;
    int site=serial/block;

    if(s->store[site].use==1)
    {
        return 0;
    }

    s->store[site].ptr=ch;
    s->store[site].use=1;
    s->store[site].error_reason=error_reason;

    return 1;
}

int send_tool_delete(Send_tool* s,int serial)
{
    int block=s->blocknum;
    int site=serial/block;

    if(s->store[site].use==0)
    {
        return 0;
    }


    s->store[site].use=0;

    return 1;
}