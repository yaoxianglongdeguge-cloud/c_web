#include "http_main.h"
#include <stdio.h>
#include <stdlib.h>

#include "../my_thread/worker_thread.h"
#include "http_analysis.h"
#include "http_ed_store.h"
#include "http_state.h"
#include "../connect_config/connect_manage.h"
#include "../timer/timer.h"
#include "../send_tool/send_tool.h"
#include "../memory_pool/memory_pool.h"
#include "../connect_fd/connect_fd.h"
#include "../data_struct/hash_3.h"

int http_main(int fd,worker* worker)
{

    Fd_Entry* fd_ob=NULL;
    Fd_Table_find(worker->fd_table,fd,&fd_ob);
    
    if(fd_ob->http_store->begin==NULL)
    {
        Http_ed_store_alloc(fd_ob->http_store,worker->store_area,2);
    }
    
        
    int r0=Http_ed_store_write(fd_ob->http_store,fd);//返回-1说明，没断开但是没数据
    int serial=fd_ob->ser_fina_send;

    while(1)
    {
        if(r0==0)
        {
            fd_close(worker,fd);//对端关闭连接
            return 1;
        }
                   

    //状态机开始检索http请求
        int state=0;
        int error_reason=0;
        char* source_end=http_state_judge(fd_ob->http_store,&state,&error_reason);//要复制的文本结尾，此处在文本中
            
            
        if(state==-1&&fd_ob->http_store->ptr_e!=fd_ob->http_store->end)
        //不够一个请求的，
        //但是还不需要更大的存储区
        {
            int r1=Http_ed_store_write(fd_ob->http_store,fd);
            if(r1==-1)
            {
                if(fd_ob->http_store->ptr_b=fd_ob->http_store->ptr_e)
                //说明连接里没数据并且暂存区也没有残留数据,所以可以释放占有的暂存区
                {
                    Http_ed_store_free(fd_ob->http_store,worker->store_area);
                }
                break;
                }
            else if(r1==0)
            {
                fd_close(worker,fd);
                return 1;
            }

        }
            
            
        else if(state==1)//解析出了一个完整请求
        {


                
            int h_size=source_end-fd_ob->http_store->begin+1;//要复制的文本长度
                
            int http_size=(h_size+sizeof(Http_analysis_1))*4;//存入一个请求，请求申请内存大小
                //其实一般来说http也就不到2kb，然后请求体又不解析，所以膨胀的话也不会很大。
                


            Http_analysis_1* h;
            Http_analysis_init(&h,worker->http_pool,http_size);

            Http_ed_store_copy(fd_ob->http_store,source_end,h->ptr);
            char* http_txt_begin=h->ptr;//复制之后等待被分割解析的文本
            
            h->ptr=h->ptr+h_size+1;
            //如果是http_analysis或者他特有的数据结构的函数，那会自己移动指针，但这个不是，需要我操作
            //加一是因为复制文本后，最后面还有个\0
                        

                
            Http_analysis_receive(h,http_txt_begin,&error_reason);

            if(error_reason!=200)
            {
                Memory_Pool_free(worker->http_pool,h,http_size);
            }
                

            http_state_reset(fd_ob->http_store);    

            fd_ob->ser_fina_send++;
            
            worker_to_profession(worker,fd,h,error_reason,serial);

            error_reason=200;
                
        }
            
            
        else if(state==-1&&fd_ob->http_store->ptr_e==fd_ob->http_store->end)
        //这个暂存区根本装不下连接发来的一个请求
        {
                Http_ed_store_expend(fd_ob->http_store,worker->store_area);//换更大的暂存区
                Http_ed_store_write(fd_ob->http_store,fd);
        }
                

        else if(state==0)// 发生了错误
        {


            fd_ob->ser_fina_send++;

            worker_to_profession(worker,fd,NULL,error_reason,serial);
            error_reason=200;
                    
            break;
        }
                      
       
   }

                
    return 1;

}



