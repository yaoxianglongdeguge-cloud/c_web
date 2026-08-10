#include<stdio.h>

typedef struct memory_pool memory_pool;

int Memory_pool_init(memory_pool* memo,int max_size,int Page_size);

void* Memory_pool_alloc(memory_pool* memo,int alloc_size);

int Memory_pool_free(memory_pool* memo,void* p);