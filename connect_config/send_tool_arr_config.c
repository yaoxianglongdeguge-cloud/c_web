#include "send_tool_arr_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>



#include "../my_thread/worker_thread.h"
#include "../memory_pool/memory_pool.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../send_tool/send_tool.h"
#include "../send_tool/send_tool_early.h"

int send_tool_arr_init(worker* work,int num,int blocknum)
{
    if(work==NULL)
    {
        return 0;
    }

    work->send_tool_table=(Send_table*)malloc(sizeof(Send_table));
    work->send_tool_table->table=(Send_table_Entry*)malloc(num*sizeof(Send_table_Entry));
    work->send_tool_table->end=num;
    work->send_tool_table->block_num=blocknum;

    work->send_tool_arr=(Send_tool**)malloc(sizeof(Send_tool*)*num);

    for(int i=0;i<num;i++)
    {
        send_tool_init(&(work->send_tool_arr[i]),blocknum);

        work->send_tool_table->table[i].fd=-2;//防止fd返回-1
        sem_init(&(work->send_tool_table->table[i].sem),0,work->send_tool_table->block_num);
        pthread_mutex_init(&(work->send_tool_table->table[i].mutex), NULL);

    }


    return 1;

}

int send_tool_arr_fdget(worker* work,int fd)
{
    int a=0;
    int which=-2;
    while(a<work->send_tool_table->end)
    {
        if(work->send_tool_table->table[a].fd==fd)
        {
            which=a;
            break;
        }
        a++;
    }


    return which;
}

int send_tool_arr_fdalloc(worker* work,int fd)//如果没分配到则返回-2
{
    int a=0;
    int can=0;//标记有没有分配到
    int which=send_tool_arr_fdget(work,fd);

    if(which!=-2)
    {
        return which;
    }
    
    while(a<work->send_tool_table->end)
    {
        if(work->send_tool_table->table[a].fd==-2)
        {
            work->send_tool_table->table[a].fd=fd;
            which=a;
            can=1;
            break;
        }
        a++;
    }

    if(can==0)
    {
        return -2;
    }

    send_tool_early_insert(work->send_early,which);

    return which;
}

int send_tool_arr_fdfree(worker* work,int fd)
{

    int which=send_tool_arr_fdget(work,fd);
    if(which==-2)
    {
        return -1;
    }

   
   
   for(int i=0;i<work->send_tool_arr[which]->blocknum;i++)
   {
    if(work->send_tool_arr[which]->store[i].use==1)
    {
        //把剩余包对应的存着返回包的内存池部分释放//当然如果是指向的返回错误信息，这个不用释放,因为其他的还要用
        if(work->send_tool_arr[which]->store[i].error_reason>0)
        {
            int sz=work->send_tool_arr[which]->store[i].error_reason;
            Memory_pool_free(work->send_pool,work->send_tool_arr[which]->store[i].ptr,work->send_tool_arr[which]->store[i].ptr+sz);
        }
    }
    work->send_tool_arr[which]->store[i].use=0;
   }

   //把连接占用的表释放出来
   work->send_tool_table->table[which].fd=-2;
   sem_init(&(work->send_tool_table->table[which].sem),0,work->send_tool_table->block_num);
   pthread_mutex_init(&work->send_tool_table->table[which].mutex, NULL);


   send_tool_early_pop(work->send_early,which);

   return 1;

}