#include "http_main.h"
#include <stdio.h>
#include <stdlib.h>

#include "../my_thread/worker_thread.h"
#include "http_analysis.h"
#include "http_ed_store.h"
#include "http_state.h"
#include "../timer/timer.h"


int Http_main(int fd,worker* worker)
{
   //timer_reset(fd);//重置计时和踢出连接

   //http_ed_store* fd_http_ed_store=fd_http_ed_store_get(worker,fd);
   int fd_store=0;
   Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);

   while(1)
   {
    int state=0;
    char* source=http_state_judge(worker->http_ed_store_arr[fd_store],&state);
    if(state==0)// 发生了错误
    {
        return -1;
    }
    else if(state==-1&&worker->http_ed_store_arr[fd_store]->ptr_e!=worker->http_ed_store_arr[fd_store]->end)
    {
       Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);
    }
    else if(state==1)
    {
        int h_size=source-worker->http_ed_store_arr[fd_store]->begin+1;
        int http_size=h_size+sizeof(Http_analysis_1)*2;//存入一个请求，请求申请内存大小
        Http_analysis_1* h;

        int e0=Http_analysis_init(h,worker->http_pool,http_size);

        if(e0!=1)
        {
            return -1;
        }

        Http_ed_store_copy(worker->http_ed_store_arr[fd_store],source,h->ptr);
        h->ptr=h->ptr+h_size;

        //http_packet* h_packet=(http_packet*)malloc(sizeof(http_packet));

        //worker_to_profession(worker,fd,h_packet);//加入任务队列


    }
    /*else if(state==-1&&fd_http_ed_store->ptr_e==fd_http_ed_store->end)//这个暂存区根本装不下连接发来的一个请求
    {
        http_ed_store_change(fd);//换更大的暂存区
        int n=Http_ed_store_accept(fd_http_ed_store,fd);
        if(n==-1)
        {
            break;
        }
    }
    else if(state==-2)//不是http协议
    {
        fd_close(fd);//断开连接
        break;
    }
    */

   }

   //连接没数据了，挂起
   //fd_list(worker,fd);

   return 1;

}


