#include <stdio.h>


/*
一个生成时固定长度的小内存池，主要用于http包解析前存放位置和解析后存放位置
*/

typedef struct m_pool_1{

    int begin;
    int cur;
    int end;
    
}m_pool_1;

int  M_pool_1_init(m_pool_1* p,int size)
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

    return 1;
}

int M_pool_destory(m_pool_1* p)
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



