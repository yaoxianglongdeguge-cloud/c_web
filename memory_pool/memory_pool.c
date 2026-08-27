#include "../include.h"


//做一个单级页表的内存池。一个页控制在4kb。
//由于是用户态的组件，所以也没有考虑崩溃，写入不一致等问题

//每页大小灵活一些，因为不同地方可能数据分散度不同

int Memory_Stack_top_and_pop(Memory_Stack* s,int* top_serial);
int Memory_Stack_push(Memory_Stack* s,int strip_serial);

int Memory_Stack_init(Memory_Stack* s,int strip_num)
{
    
    s->leisure=(int*)malloc(sizeof(int)*strip_num);
    for(int i=0;i<strip_num;i++)
    {
        s->leisure[i]=-1;
    }

    s->num=strip_num;
    s->top=0;
    pthread_mutex_init(&(s->mutex),NULL);

    return 1;
}

int Memory_Stack_expend(Memory_Entry* e,int expend_num)
{
    Memory_Stack* s=(Memory_Stack*)malloc(sizeof(Memory_Stack));
    Memory_Stack_init(s,expend_num);
    int serial=-1;
    int em=Memory_Stack_top_and_pop(e->stack,&serial);
    while(em!=0)
    {
        Memory_Stack_push(s,serial);
        em=Memory_Stack_top_and_pop(e->stack,&serial);
    }

    pthread_mutex_lock(&(s->mutex));
    free(e->stack);
    e->stack=s;
    pthread_mutex_unlock(&(s->mutex));

}

int Memory_Stack_push(Memory_Stack* s,int strip_serial)
{
    pthread_mutex_lock(&(s->mutex));
    s->leisure[s->top]=strip_serial;
    s->top++;
    pthread_mutex_unlock(&(s->mutex));

    return 1;
}

int Memory_Stack_top_and_pop(Memory_Stack* s,int* top_serial)
{
    int e=0;
    pthread_mutex_lock(&(s->mutex));
    if(s->top==0)
    {
        e=0;
    }
    else
    {
        e=1;
        s->top--;
        *top_serial=s->leisure[s->top];
        s->leisure[s->top]=-1;
    }
    pthread_mutex_unlock(&(s->mutex));

    return e;
}



int Memory_Entry_init(Memory_Entry* e,int strip_num_future,int strip_size,int init_num)
{
    e->haded_num=0;
    e->strip_num=init_num;
    e->strip_size=strip_size;
    e->memory_strip=calloc(init_num,strip_size*1024);
    e->stack=(Memory_Stack*)malloc(sizeof(Memory_Stack));
    Memory_Stack_init(e->stack,strip_num_future+50);
    for(int i=0;i<init_num;i++)
    {
    Memory_Stack_push(e->stack,i);
    }
    return 1;
}

int Memory_Entry_expend(Memory_Entry* e)
{
    int before_num=e->strip_num;
    int expend_num=before_num*2;

    int strip_size=e->strip_size;
    int total_size=before_num*strip_size*1024;

    char* strip=(char*)malloc(expend_num*strip_size*1024);
    memcpy(strip,e->memory_strip,total_size);
    char* p=e->memory_strip;
    e->memory_strip=strip;
    free(p);
    e->strip_num=e->strip_num*2;

    
    for(int i=before_num;i<expend_num;i++)
    {
        Memory_Stack_push(e->stack,i);
    }
    

    Memory_Stack_expend(e,expend_num+100);
    
    return 1;
    
}

int Memory_Entry_alloc(Memory_Entry* e,void** ptr)
{
    if(e->haded_num==e->strip_num)
    {
        Memory_Entry_expend(e);
    }
    int size=e->strip_size*1024;
    int serial=-1;
    Memory_Stack_top_and_pop(e->stack,&serial);
    *ptr=(void*)(e->memory_strip+serial*size);
    e->haded_num++;

    return 1;
}

int Memory_Entry_alloc2(Memory_Entry* e,void** ptr,int* notfull)
{
    *notfull=0;
    if(e->haded_num==e->strip_num)
    {
        return 1;
    }

    int size=e->strip_size*1024;
    int serial=-1;
    Memory_Stack_top_and_pop(e->stack,&serial);
    *ptr=(void*)(e->memory_strip+serial*size);
    e->haded_num++;
    *notfull=1;

    return 1;
}

int Memory_Entry_free(Memory_Entry* e,void* ptr)
{
    int size=e->strip_size;
    int total_gap=(char*)ptr-e->memory_strip;
    int serial=total_gap/(size*1024);

    Memory_Stack_push(e->stack,serial);
    e->haded_num--;

    return 1;
}



int Memory_Pool_init(Memory_Pool** p,int max_num,int init_max,int strip_num_future,int init_strip_num)//可以申请最大数量，但是只有前几个大小的初始化几条
{
    *p=(Memory_Pool*)malloc(sizeof(Memory_Pool));
    (*p)->Entry_num=max_num;
    (*p)->pool=(Memory_Entry*)malloc(max_num*sizeof(Memory_Entry));

    int strip_size=1;
    int init_num=0;

    for(int i=0;i<max_num;i++)
    {
        if(i<init_max)
        {
            init_num=init_strip_num;
        }
        else
        {
            init_num=0;
        }

        Memory_Entry_init(&((*p)->pool[i]),strip_num_future,strip_size,init_num);
        strip_size=strip_size*2;
    }

    return 1;


}

int Memory_Pool_alloc(Memory_Pool* p,int size,void** ptr,int* notfull)
{
    *notfull=0;

    if(size>p->pool[p->Entry_num-1].strip_size)
    {
        *notfull=1;
        *ptr=malloc(size*1024);
        return 1;
    }
    int alloc_size=1;
    int i=0;
    for(;i<p->Entry_num;i++)
    {
        if(alloc_size>=size)
        {
            break;
        }
        alloc_size=alloc_size*2;
    }

    Memory_Entry* aim_entry=&(p->pool[i]);
    int notfull2=1;
    Memory_Entry_alloc2(aim_entry,ptr,&notfull2);
    if(notfull2==1&&*ptr!=NULL)
    {
        *notfull=1;
    }
        

    return 1;
}

int Memory_Pool_free(Memory_Pool* p,void* ptr,int size)//单位字节
{
    if(size>p->pool[p->Entry_num-1].strip_size*1024)
    {
        free(ptr);
        return 1;
    }
    int alloc_size=1;
    int i=0;
    for(;i<p->Entry_num;i++)
    {
        if(alloc_size*1024>=size)
        {
            break;
        }
        alloc_size=alloc_size*2;
    }
    Memory_Entry* aim_entry=&(p->pool[i]);
    int right_size=aim_entry->strip_size*aim_entry->strip_num*1024;
    if((char*)ptr<aim_entry->memory_strip||(char*)ptr+size-1>aim_entry->memory_strip+right_size)
    {
        return 0;
    }

    Memory_Entry_free(aim_entry,ptr);

    return 1;

}

