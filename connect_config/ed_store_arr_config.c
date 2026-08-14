#include "ed_store_arr_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../send_tool/send_tool.h"

int ed_store_arr_init(worker* work,int small_num,int small_size,int big_num,int big_size)//注册多少条大的或小的，大的多大小的多小
{
    big_size=big_size*1024;
    small_size=small_size*1024;
    if(work==NULL)
    {
        return 0;
    }
    int total_num=small_num+big_num;
    work->store_pool_table=(Store_table*)malloc(sizeof(Store_table));
    work->store_pool_table->table=(int*)malloc(total_num*sizeof(int));
    work->store_pool_table->cut=small_num;
    work->store_pool_table->end=total_num;
    work->store_pool_table->small_block_num=small_size;
    work->store_pool_table->big_block_num=big_size;

    for(int i=0;i<small_num;i++)
    {
        work->http_ed_store_arr[i]=malloc(small_size);
        work->http_ed_store_arr[i]->begin=(char*)(work->http_ed_store_arr[i]);
        work->http_ed_store_arr[i]->end=work->http_ed_store_arr[i]->begin+small_size;
        work->http_ed_store_arr[i]->ptr_b=work->http_ed_store_arr[i]->begin;
        work->http_ed_store_arr[i]->ptr_e=work->http_ed_store_arr[i]->ptr_b;

        int a=http_state_init(&(work->http_ed_store_arr[i]->httpstate));
        if(a!=1)
        {
            return 1;
        }

        work->store_pool_table->table[i]=-2;//防止fd返回-1

    }

    for(int j=0;j<big_num;j++)
    {
        int i=j+work->store_pool_table->cut;
        work->http_ed_store_arr[i]=malloc(big_size);
        work->http_ed_store_arr[i]->begin=(char*)(work->http_ed_store_arr[i]);
        work->http_ed_store_arr[i]->end=work->http_ed_store_arr[i]->begin+big_size;
        work->http_ed_store_arr[i]->ptr_b=work->http_ed_store_arr[i]->begin;
        work->http_ed_store_arr[i]->ptr_e=work->http_ed_store_arr[i]->ptr_b;

        int a=http_state_init(&(work->http_ed_store_arr[i]->httpstate));
        if(a!=1)
        {
            return 1;
        }

        work->store_pool_table->table[i]=-2;//防止fd返回-1

    }

    return 1;

}

int ed_store_pool_fdget(worker* work,int fd)
{
    int a=0;
    int which=-2;
    while(a<work->store_pool_table->end)
    {
        if(work->store_pool_table->table[a]==fd)
        {
            which=a;
        }
        a++;
    }

    return which;
}

int ed_store_pool_fdalloc(worker* work,int fd)//如果没分配到则返回-2
{
    int a=0;
    int can=0;//标记有没有分配到
    int which=ed_store_pool_fdget(work,fd);
    if(which!=-2)
    {
        return which;
    }
    
    while(a<work->store_pool_table->cut)
    {
        if(work->store_pool_table->table[a]==-2)
        {
            work->store_pool_table->table[a]=fd;
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

int ed_store_pool_fdchange(worker* work,int fd)
{

    //确认有这个东西
    int a=0;
    int which=ed_store_pool_fdget(work,fd);
    if(which==-2)
    {
        return -1;
    }
    //确认此时不在大条中
    if(which>=work->store_pool_table->cut)
    {
        return 0;
    }

    work->store_pool_table->table[which]=-2;

    //分配大条
    int a2=work->store_pool_table->cut;
    int can=0;//标记有没有分配到
    int which2=-2;
    while(a<work->store_pool_table->end)
    {
        if(work->store_pool_table->table[a2]==-2)
        {
            work->store_pool_table->table[a]=fd;
            which2=a;
            can=1;
        }
        a++;
    }

    if(can==0)
    {
        return -2;
    }

    int char_num=work->http_ed_store_arr[which]->ptr_e-work->http_ed_store_arr[which]->ptr_b;
    memcpy(work->http_ed_store_arr[which]->ptr_b,work->http_ed_store_arr[which2]->begin,char_num);
    work->http_ed_store_arr[which2]->ptr_e=work->http_ed_store_arr[which2]->begin+char_num;

    ed_store_pool_fdfree(work,fd);

    return which2;

}

int ed_store_pool_fdfree(worker* work,int fd)
{
    int which=ed_store_pool_fdget(work,fd);
    if(which==-2)
    {
        return -1;
    }
   
   work->http_ed_store_arr[which]->ptr_b=work->http_ed_store_arr[which]->begin;
   work->http_ed_store_arr[which]->ptr_e=work->http_ed_store_arr[which]->ptr_b;
   work->store_pool_table->table[which]=-2;
   http_state_reset(work->http_ed_store_arr[which]);

   return 1;

}





