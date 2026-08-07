#include "memory_pool/memory_pool_1.h"

int  M_pool_1_init(m_pool_1* p,int size,int count)
{
    p=(m_pool_1*)malloc(sizeof(m_pool_1));
    if(p==NULL)
    {
        return -1;
    }

    p->begin=malloc(size);
    if(p->begin==NULL)
    {
        return -1;
    }

    p->cur=p->begin;
    p->end=p->begin+size;
    p->count=count;
    p->count_max=count;
    p->size=size;

    return 1;
}

int M_pool_1_destory(m_pool_1* p)
{
    free(p->begin);
    if(p->begin!=NULL)
    {
        return -1;
    }
    free(p);
    if(p!=NULL)
    {
        return -1;
    }

    return 1;
}

void* M_pool_1_alloc(m_pool_1** p,int size)
{
    if((*p)->cur+size>(*p)->end)
    {
        int a=M_pool_1_expend(p);
        if(a!=1)
        {
            return NULL;
        }
        (*p)->count=(*p)->count_max;
    }
    
    void* ptr=(*p)->cur;
    (*p)->cur=(*p)->cur+size;
    (*p)->alloc_size=(*p)->alloc_size+size;

    return ptr;

}

int M_pool_1_recycle(m_pool_1** p)
{
    if(*p==NULL)
    {
        return 0;
    }

    int m_size=(*p)->size;
    int n_size=(*p)->alloc_size;

    (*p)->cur=(*p)->begin;
    (*p)->alloc_size=0;

    if(m_size>n_size*2)
    {
        (*p)->count--;
    }

    if((*p)->count<=0)
    {
        int a=M_pool_1_narrow(p);
        if(a!=1)
        {
            return -1;
        }
        (*p)->count=(*p)->count_max;
    }

    return 1;

}

int M_pool_1_expend(m_pool_1** pm)
{
    m_pool_1* m=(m_pool_1*)malloc(sizeof(m_pool_1));
    int a=M_pool_1_init(m,((*pm)->size)*2,0);
    if(a!=1)
    {
        return -1;
    }

    m->cur=m->begin+(*pm)->alloc_size;
    m->alloc_size=(*pm)->alloc_size;

    *pm=m;

    return 1;
    
}

int M_pool_1_narrow(m_pool_1** pm)
{
    m_pool_1* m=(m_pool_1*)malloc(sizeof(m_pool_1));
    int a=M_pool_1_init(m,((*pm)->size)/2,0);
    if(a!=1)
    {
        return -1;
    }

    m->cur=m->begin;
    m->alloc_size=0;

    *pm=m;

    return 1;

}
