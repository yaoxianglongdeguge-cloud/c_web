#include "send_tool_arr_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h> 
#include <pthread.h> 


#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../send_tool/send_tool.h"


int send_tool_arr_init(worker* work,int num,int size)//注册多少条大的或小的，大的多大小的多小
{
    size=size*1024;
    if(work==NULL)
    {
        return 0;
    }
    int total_num=num;
    work->send_tool_table=(Send_table*)malloc(sizeof(Send_table));
    work->send_tool_table->table=(Send_table_Entry*)malloc(total_num*sizeof(Send_table_Entry));
    work->send_tool_table->end=total_num;
    work->send_tool_table->block_num=size/sizeof(char*);

    for(int i=0;i<num;i++)
    {
        work->send_tool_arr[i]=malloc(size);
        work->send_tool_arr[i]->spack=(char**)(work->send_tool_arr[i]);

        work->send_tool_table->table[i].fd=-2;//防止fd返回-1
        sem(&(work->send_tool_table->table[i].sem),0,work->send_tool_table->block_num);
        pthread_mutex_init(&work->send_tool_table->table[i].mutex, NULL);

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
        }
        a++;
    }

    if(can==0)
    {
        return -2;
    }

    return which;
}

int send_tool_arr_fdfree(worker* work,int fd)
{
    int which=send_tool_arr_fdget(work,fd);
    if(which==-2)
    {
        return -1;
    }
   
   work->send_tool_arr[which]->ptr_b=work->send_tool_arr[which]->begin;
   work->send_tool_arr[which]->ptr_e=work->send_tool_arr[which]->ptr_b;

   work->send_tool_table->table[which].fd=-2;
   sem(&(work->send_tool_table->table[which].sem),0,work->send_tool_table->block_num);
   pthread_mutex_init(&work->send_tool_table->table[which].mutex, NULL);


   return 1;

}