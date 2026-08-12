#include "memory_pool.h"
#include <stdlib.h>
#include <stdio.h>



//做一个单级页表的内存池。一个页控制在4kb。
//由于是用户态的组件，所以也没有考虑崩溃，写入不一致等问题

//每页大小灵活一些，因为不同地方可能数据分散度不同

typedef struct Page{

    char* Page_begin;//页的起始位置。
    int p_size;
    int use;//使用情况

}Page;//最小分配单元页的表项，这个并不是指向页本身，里面的指针才是指向页本身

typedef Page* Page_table;


int Memory_pool_init(memory_pool** memo,int max_size,int Page_size)//这里的size单位是kb
{
    (*memo)=(memory_pool*)malloc(sizeof(memory_pool));
    int Bytes_size=max_size*1024;
     (*memo)->All_begin=NULL;
     (*memo)->All_end=NULL;
     (*memo)->m_size=max_size;
     (*memo)->Page_size=Page_size;
     (*memo)->All_begin=malloc(Bytes_size);
    if( (*memo)->All_begin==NULL)
    {
        return -1;
    }

     (*memo)->All_end= (*memo)->All_begin+Bytes_size;

    //写入页表
    Page_table p1=(Page_table) (*memo)->All_begin;
    int table_num=max_size/Page_size;//页表项的个数
    for(int i=0;i<table_num;i++)
    {
        p1[i].p_size=4;
        p1[i].Page_begin=i*Page_size*1024+ (*memo)->All_begin;//计算每一页的虚拟地址位置
        p1[i].use=0;//0代表没有被利用
    }
    //计算一个页表表项大小kb，后面要确定哪些表被占用了

    //int page_table_size=sizeof(Page)/1024;//页表项最多0.几kb，这里用整数类型导致变成了0，就导致了下面计算
    //结果成了0
    //int used_page=table_num*page_table_size/Page_size;//用来存储页表项的页数

    int page_table_size=sizeof(Page);
    int used_page=(table_num*page_table_size/((Page_size)*1024))+1;//要向上取整

    for(int i=0;i<used_page;i++)
    {
        p1[i].use=1;
    }

    return 1;

}

void* Memory_pool_alloc(memory_pool* memo,int alloc_size)//这里的size是字节
{
    int table_num=memo->m_size/memo->Page_size;
    void* j0=NULL;
    if(alloc_size<=memo->Page_size*1024)
    {
        for(int i=0;i<table_num;i++)
        {
            Page_table p0=(Page_table)(memo->All_begin+i*sizeof(Page));
            if(p0->use==0)
            {
                j0=p0->Page_begin;
                p0->use=1;
                break;
            }

        }
    }
    else if(alloc_size>memo->Page_size*1*1024&&alloc_size<=memo->Page_size*2*1024)//如果有需要，也可以连续分配三个甚至更多
    {
        int i0=0;
        int i1=1;
        
        while(i1<table_num)
        {
            Page* p0=(Page_table)(memo->All_begin+i0*sizeof(Page));
            Page* p1=(Page_table)(memo->All_begin+i1*sizeof(Page));
            if(p0->use==0&&p1->use==0)
            {
                j0=p0->Page_begin;
                p0->use=1;
                p1->use=1;
                break;
            }

            i0++;
            i1++;
            
        }
    }
    else if(alloc_size>memo->Page_size*2*1024)
    {
        j0=malloc(alloc_size);
    }

    return j0;

}

int Memory_pool_free(memory_pool* memo,void* p,void* end)//end是为了能自主设定把哪一页到哪一页全部释放，完全符合符合顺序操作的目的
{
    int table_num=memo->m_size/memo->Page_size;
    char* ptr=p;

    int p_table=(ptr-memo->All_begin)/1024/memo->Page_size;
    int end_table=(ptr-memo->All_begin)/1024/memo->Page_size;

    if(p_table<0||p_table>=table_num)//p不在内存池里
    {
        free(ptr);
    }
    else if(end_table<0||end_table>=table_num)
    {
        return 0;
    }
    else
    {
        Page* which_table=(Page_table)(memo->All_begin+p_table*sizeof(Page));//p的表项
        int gap=end_table-p_table+1;//从p页到end页都释放掉

        for(int i=0;i<gap;i++)
        {
            which_table[i].use=0;
        }
    }

    return 1;
}



