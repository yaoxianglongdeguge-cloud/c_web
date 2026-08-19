#include"send_tool.h"
#include <stdio.h>
#include <stdlib.h>
#include "../memory_pool/memory_pool.h"

int send_tool_init(Send_tool** s)
{
    *s=(Send_tool*)malloc(sizeof(Send_tool));
    (*s)->store=NULL;
    (*s)->blocknum=0;

    return 1;
}

int send_tool_alloc(Send_tool* s,Memory_Pool* pool,int blocknum)
{
    s->blocknum=blocknum;
    void* ptr=NULL;
    int size=sizeof(Send_tool_Entry)*blocknum;
    Memory_Pool_alloc(pool,size,&ptr);
    s->store=(Send_tool_Entry*)ptr;

    for(int i=0;i<blocknum;i++)
    {
        s->store[i].ptr=NULL;
        s->store[i].error_reason=0;
        s->store[i].use=0;
        s->store[i].size=0;
    }

    return 1;
}

int send_tool_free(Send_tool* s,Memory_Pool* pool)
{
    int size=s->blocknum*sizeof(Send_tool_Entry);
    Memory_Pool_free(pool,s->store,size);
    return 1;
}

int send_tool_destory(Send_tool* s,Memory* pool)
{
    if(s->store!=NULL)
    {
        Memory_Pool_free(pool,s->store,size);
    }

    free(s);
    return 1;
}
