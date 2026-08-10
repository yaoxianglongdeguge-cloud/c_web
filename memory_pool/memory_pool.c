#include "memory_pool/memory_pool.h"


//做一个单级页表的内存池。一个页控制在4kb。
//由于是用户态的组件，所以也没有考虑崩溃，写入不一致等问题

#define Max_size 1024
#define Max_Page_level 2


typedef struct memory_pool{

    char* All_begin;//内存池起始位置
    char* All_end;
    int m_size;//内存池总大小

}memory_pool;

typedef struct Page{

    char* Page_begin;//页的起始位置。
    int p_size;
    int use;//使用情况

}Page;//这个页不仅是最小分配单元页，而且也可以指高级页表指向的一整块更大的页，而每次分配页，都要向上反馈，对保有内存进行调整

typedef struct Page_table{

    Page* Next_page;//指向下级页表或页的数组指针

}Page_table;


int Memory_pool_init(memory_pool* memo,int max_size,int )



