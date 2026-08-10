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

}Page;//最小分配单元页

typedef Page* Page_table;


int Memory_pool_init(memory_pool* memo,int max_size)
{
    int Bytes_size=max_size*1024;
    memo->All_begin=NULL;
    memo->All_end=NULL;
    memo->m_size=max_size;
    memo->All_begin=malloc(Bytes_size);
    if(memo->All_begin==NULL)
    {
        return -1;
    }

    memo->All_end=memo->All_begin+Bytes_size;

    //写入页表
    Page_table p1=memo->All_begin;
    int table_num=max_size/4;
    for(int i=0;i<table_num;i++)
    {
        p1[i].p_size=4;
        p1[i].Page_begin=i*4*1024+memo->All_begin;//计算每一页的虚拟地址位置
        p1[i].use=0;//0代表没有被利用
    }
    //计算一个页表表项大小，后面要确定哪些表被占用了
    int page_table_size=sizeof(Page)/1024;
    int used_page=table_num*page_table_size/4;
    for(int i=0;i<used_page;i++)
    {
        p1[i].use=1;
    }

    return 1;

}

int 



