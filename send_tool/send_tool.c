#include "../include.h"

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
    int size=sizeof(Send_tool_Entry)*blocknum/1024+1;
    int notfull=0;
    Memory_Pool_alloc(pool,size,&ptr,&notfull);
    if(notfull==0)
    {
        return -1;
    }
    s->store=(Send_tool_Entry*)ptr;

    for(int i=0;i<blocknum;i++)
    {
        s->store[i].m_queue=NULL;
        s->store[i].ptr=NULL;
        s->store[i].error_reason=0;
        s->store[i].use=0;
        s->store[i].size_resp=0;
        s->store[i].offset=0;
        s->store[i].send_fd=-1;
        s->store[i].size_file=0;
        s->store[i].off=0;
    }

    return 1;
}

int send_tool_free(Send_tool* s,Memory_Pool* pool)
{
    if(s->store==NULL||s->blocknum==0)
    {
        return 1;
    }
    int size=s->blocknum*sizeof(Send_tool_Entry);
    Memory_Pool_free(pool,s->store,size);
    s->store=NULL;
    s->blocknum=0;
    return 1;
}

int send_tool_destory(Send_tool* s,Memory_Pool* pool)
{
    int size=s->blocknum*sizeof(Send_tool_Entry);
    if(s->store!=NULL)
    {
        Memory_Pool_free(pool,s->store,size);
    }

    free(s);
    return 1;
}
