#include "http_back_order.h"
#include <stdio.h>
#include <stdlib.h>
#include "../data_struct/hash_3.h"


int http_back_order_init(Http_back_order** h,int init)//内部哈希表大小型号
{
    *h=(Http_back_order*)malloc(sizeof(Http_back_order));
    (*h)->num=0;
    int e0=Hash3_Init(&((*h)->order),init);
    if(e0!=1)
    {
        return -1;
    }

    return 1;
}

 http_back_order_get(Http_back_order* h,int fd,int* error)
{
    error=0;
    int e0=0;
    Hash_Entry_3* e=Hash3_Find(h->order,fd,&e0);
    if(e0!=1)
    {
        error=-1;
    }

    error=1;

    return e;
}

int http_back_order_add(Http_back_order* h,int fd,int which)
{
    int e0;
    Hash_Entry_3* e=Hash3_Find(h->order,fd,&e0);
    if(e0!=1)
    {
        return -1;
    }

    switch (which)
    {
    case 1:
        e->value1++;
        break;
    case 2:
        e->value2++;
        break;
    case 3:
        e->value3++;
        break;
    
    default:
        break;
    }

    return 1;
}

int http_back_order_insertfd(Http_back_order* h,int fd)
{
    int e0=Hash3_Insert(h->order,fd,0,0,0);
    if(e0!=1)
    {
        return -1;
    }
    h->num++;

    return 1;
}

int http_back_order_deletefd(Http_back_order* h,int fd)
{
    int e0=Hash3_Delete(h->order,fd);
    if(e0!=1)
    {
        return -1;
    }

    h->num--;

    return 1;
}

