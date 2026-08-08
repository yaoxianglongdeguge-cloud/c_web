#include <stdio.h>

/*
一个生成时固定长度的小内存池，主要用于http包解析前存放位置和解析后存放位置.这个内存池是针对同时存入和同时释放的
*/

typedef struct m_pool_1{

    char* begin;
    char* cur;
    char* end;
    int size;//总内存大小
    int alloc_size;//分配的内存
    int count;//计数器，每次统一回收，如果分配内存少于整块内存二分之一，就减一，减到零缩容
    int count_max;

}m_pool_1;

int  M_pool_1_init(m_pool_1* p,int size,int count);

int M_pool_1_destory(m_pool_1* p);

void* M_pool_1_alloc(m_pool_1** p,int size);//分配内存

int M_pool_1_recycle(m_pool_1** p);//回收内存




