#include "http_main.h"
#include <stdio.h>
#include <stdlib.h>

#include "../my_thread/worker_thread.h"
#include "http_analysis.h"
#include "http_ed_store.h"
#include "http_state.h"
#include "connect_config/ed_store_arr_config.h"
#include "../timer/timer.h"


int Http_main(int fd,worker* worker)
{

   int fd_store=ed_store_pool_fdalloc(worker,fd);
   
   if(fd_store>=0)
   {

       int r0=Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);//返回-1说明，没断开但是没数据
       printf(worker->http_ed_store_arr[fd_store]->begin);
       
       while(1)
       {
           if(r0==-1)
           {
               ed_store_pool_fdfree(worker,fd);
               break;
            }
            
            int state=0;
            char* source_end=http_state_judge(worker->http_ed_store_arr[fd_store],&state);//要复制的文本结尾，此处在文本中
            
            
            if(state==-1&&worker->http_ed_store_arr[fd_store]->ptr_e!=worker->http_ed_store_arr[fd_store]->end)//不够一个请求的，
            //但是还不需要更大的存储区
            {
                int r1=Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);
                if(r1==-1)
                {
                    if(worker->http_ed_store_arr[fd_store]->begin==worker->http_ed_store_arr[fd_store]->end)
                    //说明连接里没数据并且暂存区也没有残留数据,所以可以释放占有的暂存区
                    {
                        ed_store_pool_fdfree(worker,fd);
                    }
                    break;
                }
            }
            
            
            else if(state==1)//解析出了一个完整请求
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
                
                Http_ed_store_write(worker->http_ed_store_arr[fd_store],fd);
                
                Http_analysis_receive(h,http_txt_begin);
                
                http_state_reset(worker->http_ed_store_arr[fd_store]);
                
                
                //http_packet* h_packet=(http_packet*)malloc(sizeof(http_packet));
                
                //worker_to_profession(worker,fd,h_packet);//加入任务队列
                
            }
            
            
            else if(state==-1&&worker->http_ed_store_arr[fd_store]->ptr_e==worker->http_ed_store_arr[fd_store]->end)
            //这个暂存区根本装不下连接发来的一个请求
            {
                ed_store_pool_fdchange(worker,fd);//换更大的暂存区
                int n=Http_ed_store_accept(worker->http_ed_store_arr[fd_store],fd);
                if(n==-1)
                {
                    break;
                }
            }
            
                /*else if(state==-2)//不是http协议,或者有错误
                {
                    fd_close(fd);//断开连接
                    break;
                }
                
                if(state==0)// 发生了错误
                {
                    fd_close(fd);
                    break;
                    }
                    */
                   
                   
                }
            }
                
    //timer_reset(fd);//重置计时和踢出连接
                
    return 1;

}


