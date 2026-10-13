#include "../../include.h"

int SendPointerPoolInit(Send_Pointer_Pool** s)
{
    *s=(Send_Pointer_Pool*)malloc(sizeof(Send_Pointer_Pool));
    (*s)->store=NULL;
    (*s)->blocknum=0;

    return 1;
}

int SendPointerPoolAlloc(Send_Pointer_Pool* s,Memory_Pool* pool,int blocknum)
{
    s->blocknum=blocknum;
    void* ptr=NULL;
    int size=sizeof(Send_Pointer_Pool_Entry)*blocknum/1024+1;
    int notfull=0;
    MemoryPoolAlloc(pool,size,&ptr,&notfull);
    if(notfull==0)
    {
        return -1;
    }
    s->store=(Send_Pointer_Pool_Entry*)ptr;

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

//把send池里残留的返回请求通知给业务线程
int SendPointerPoolClear(Send_Pointer_Pool* s){

    for(int i=0;i<s->blocknum;i++)
    {
        if(s->store[i].use==1)
        {
            Worker_Queue* m=s->store[i].m_queue;
            int size=s->store[i].size_resp;
            char* c=s->store[i].ptr;
            
            if(m!=NULL)
            {
                WorkerQueuePush(m,size,c);
            }

            if(s->store[i].send_fd!=-1)
            {
                close(s->store[i].send_fd);
            }
        }
    }
}

int SendPointerPoolFree(Send_Pointer_Pool* s,Memory_Pool* pool)
{
    if(s->store==NULL||s->blocknum==0)
    {
        return 1;
    }
    int size=s->blocknum*sizeof(Send_Pointer_Pool_Entry);
    MemoryPoolFree(pool,s->store,size);
    s->store=NULL;
    s->blocknum=0;
    return 1;
}

int SendPointerPoolDestory(Send_Pointer_Pool* s,Memory_Pool* pool)
{
    if(s->store!=NULL)
    {
        SendPointerPoolClear(s);
        SendPointerPoolFree(s,pool);
    }

    free(s);
    return 1;
}
