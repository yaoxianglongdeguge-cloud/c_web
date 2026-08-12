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
   printf(worker->http_ed_store_arr[fd_store]->begin);

   while(1)
   {
    int state=0;
    char* source_end=http_state_judge(worker->http_ed_store_arr[fd_store],&state);//要复制的文本结尾，此处在文本中
    if(state==0)// 发生了错误
    {
        return -1;
    }
    else if(state==-1&&worker->http_ed_store_arr[fd_store]->ptr_e!=worker->http_ed_store_arr[fd_store]->end)
    {
       Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);
    }
//------------------------------------------------------------------------------------------------
    else if(state==1)
    {

        int h_size=source_end-worker->http_ed_store_arr[fd_store]->begin+1;//要复制的文本长度

        int http_size=(h_size+sizeof(Http_analysis_1))*4;//存入一个请求，请求申请内存大小
        //其实一般来说http也就不到2kb，然后请求体又不解析，所以膨胀的话也不会很大。

        Http_analysis_1* h;

        int e0=Http_analysis_init(&h,worker->http_pool,http_size);

        if(e0!=1)
        {
            return -1;
        }

        Http_ed_store_copy(worker->http_ed_store_arr[fd_store],source_end,h->ptr);
        char* http_txt_begin=h->ptr;//复制之后等待被分割解析的文本

        h->ptr=h->ptr+h_size+1;//如果是http_analysis或者他特有的数据结构的函数，那会自己移动指针，但这个不是，需要我操作
        //加一是因为复制文本后，最后面还有个\0
        if(h->ptr>=h->end)
        {
            return 0;
        }


        Http_analysis_receive(h,http_txt_begin);

        char* method=Http_analysis_get(h,"Method",NULL);
        char* version=Http_analysis_get(h,"Version",NULL);
        char* url=Http_analysis_get(h,"Url",NULL);
        char* body=Http_analysis_get(h,"Body",NULL);
        char* headers=Http_analysis_get(h,"Headers","Sec-Fetch-Site");
        char* query1=Http_analysis_get(h,"Query","q");
        char* query2=Http_analysis_get(h,"Query","page");

        printf("Method:   %p\n", *method);
        printf("Url:      %p\n", *url);
        printf("Version:  %p\n", *version);

        Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);

        Http_analysis_receive(h,http_txt_begin);
 
        

        http_state_reset(worker->http_ed_store_arr[fd_store]);
    

        //http_packet* h_packet=(http_packet*)malloc(sizeof(http_packet));

        //worker_to_profession(worker,fd,h_packet);//加入任务队列
        
    }
    //-------------------------------------------------------------------------------------------
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


