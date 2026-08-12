

typedef struct memory_pool{

    char* All_begin;//内存池起始位置
    char* All_end;
    int m_size;//内存池总大小,单位是kb
    int Page_size;

}memory_pool;

int Memory_pool_init(memory_pool** memo,int max_size,int Page_size);

void* Memory_pool_alloc(memory_pool* memo,int alloc_size);

int Memory_pool_free(memory_pool* memo,void* p,void* end);