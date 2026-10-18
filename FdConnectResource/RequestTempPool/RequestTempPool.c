#include "../../Include/Include.h"

//暂存块超过这个大小就改用 malloc（大请求扩容时，池里同时存在旧块+新块会超过 2MB 子池）
#define REQUEST_TEMP_POOL_MAX (1024*1024)


int RequestTempPoolInit(Request_Temp_Pool** h)//分配整个存储大小，单位是字节
{
    (*h)=(Request_Temp_Pool*)malloc(sizeof(Request_Temp_Pool));
  ;
    (*h)->begin=NULL;
    (*h)->end=(*h)->begin;
    (*h)->ptr_b=(*h)->begin;
    (*h)->ptr_e=(*h)->begin;
    (*h)->from_malloc=0;

    State_Machine* ht=NULL;
    int a=StateMachineInit(&ht);
    if(a!=1)
    {
        return -1;
    }

    (*h)->state_machine=ht;

    return 1;

}

int RequestTempPoolDestory(Request_Temp_Pool* h,Memory_Pool* pool)
{
    if(h->begin!=NULL)
    {
        RequestTempPoolFree(h,pool);
    }
    
    if(h->state_machine!=NULL)
    {
        free(h->state_machine);
        h->state_machine=NULL;
    }

    free(h);
    return 1;
}

int RequestTempPoolAlloc(Request_Temp_Pool* h,Memory_Pool* pool,int size)//单位是kb
{
     void* ptr=NULL;
     int notfull=0;
    MemoryPoolAlloc(pool,size*1024,&ptr,&notfull);
    if(notfull==0)
    {
        return -1;
    }

    h->begin=(char*)ptr;
    h->end=h->begin+(size*1024)-1;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;
    h->from_malloc=0;

    return 1;
}

int RequestTempPoolFree(Request_Temp_Pool* h,Memory_Pool* pool)
{
    if(h->begin!=NULL){
        if(h->from_malloc==1) free(h->begin);
        else                  MemoryPoolFree(pool,h->begin);
    }

    h->from_malloc=0;
    h->begin=NULL;
    h->end=h->begin;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;

    RequestTempPoolStateReset(h);

    return 1;
}

int RequestTempPoolExpendTo(Request_Temp_Pool* h,Memory_Pool* pool,int needSize,int* hadBigest)
{
    int cursize=h->end-h->begin+1;          //当前缓冲总字节（end 是最后一个可用字节）
    int newsize=needSize;

    //没有精确需求（或精确值不大于当前）→ 按 2 倍扩（保持原行为）
    if(newsize<=cursize) newsize=cursize*2;

    newsize=((newsize+1023)/1024)*1024;      //按 KB 取整

    int ptr_b=h->ptr_b-h->begin;
    int ptr_e=h->ptr_e-h->begin;
    void* ptr=NULL;
    int notfull=0;

    int newFromMalloc=0;

    //超过 REQUEST_TEMP_POOL_MAX 时走 malloc：避免"旧块 + 新块"同时占子池把 2MB 撑爆
    if(newsize<=REQUEST_TEMP_POOL_MAX){
        MemoryPoolAlloc(pool,newsize,&ptr,&notfull);
        if(notfull!=0) newFromMalloc=0;
        else ptr=NULL;
    }
    if(ptr==NULL){
        ptr=malloc(newsize);
        if(ptr==NULL){ *hadBigest = 1; return -1; }
        newFromMalloc=1;
    }

    memcpy(ptr,h->begin,ptr_e);              //只拷有效数据，不整块拷
    RequestTempPoolFree(h,pool);

    h->begin=(char*)ptr;
    h->end=h->begin+newsize-1;
    h->ptr_b=h->begin+ptr_b;
    h->ptr_e=h->begin+ptr_e;
    h->from_malloc=newFromMalloc;

    return 1;
}

int RequestTempPoolExpend(Request_Temp_Pool* h,Memory_Pool* pool,int* hadBigest)
{
    return RequestTempPoolExpendTo(h,pool,0,hadBigest);   //0 = 按 2 倍扩
}

int RequestTempPoolWrite(Request_Temp_Pool* h,int fd)
{
   
    int length=h->end-h->ptr_e;
    int n1=read(fd,h->ptr_e,length);

    if(n1>0)
    {
        h->ptr_e=h->ptr_e+n1;

    }
    else if(n1==0)
    {
        return 0;
    }
    else if(n1==-1&&errno==EAGAIN)
    {
        return -1;
    }

    return 1;
}

int RequestTempPoolCopy(Request_Temp_Pool* h,char*end,char* target)//把一整个请求拷走，然后把后面的数据拷到开头
{
    if(end>=h->end)
    {
        return 0;
    }
    int copy_num=end-h->begin+1;
    memcpy(target, h->begin, copy_num);
    target[copy_num]='\0';

    h->ptr_b=h->begin+copy_num;//指针指向剩下的，也就是

    
    if(h->ptr_b[0]=='\r'&&h->ptr_b[1]=='\n'&&h->ptr_b[2]=='\r'&&h->ptr_b[3]=='\n')
    {
        h->ptr_b=h->ptr_b+4;//防止复制后参与分隔符
    }
    
    int copy_num2=h->ptr_e-h->ptr_b;
    if(copy_num2!=0)
    {
        memcpy(h->begin,h->ptr_b,copy_num2);
    }
    
    h->ptr_b=h->begin;
    h->ptr_e=h->ptr_b+copy_num2;
    *(h->ptr_e)='\0';

    return 1;
}

int RequestTempPoolStateReset(Request_Temp_Pool* hs)
{
    hs->state_machine->h_body_length=0;
    hs->state_machine->h_method=0;
    hs->state_machine->h_rnrn=0;

    return 1;
}





