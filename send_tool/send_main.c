#include "send_main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../memory_pool/memory_pool.h"
#include "../my_thread/worker_thread.h"
#include "../timer/timer.h"
#include "send_tool.h"
#include "../queue/send_thing_queue.h"
#include "../connect_fd/connect_fd.h"
#include "../data_struct/hash_3.h"
#include "../connect_config/connect_manage.h"
#include "../http_analysis/http_analysis.h"
#include "../http_analysis/http_ed_store.h"
#include "../queue/memory_queue.h"


int send_main(worker* w,int ed_store_blocknum)
{
    int send_fd=-2;
    Send_tq_Entry s;
    //这里是看返回事件队列里能现在立刻发的包，然后顺便发他后面顺延的包

    int e0=1;
    while(e0!=0&&send_fd==-2)
    {
        s = Send_thing_queue_top_and_pop(w->Thing_queue,&e0);
        if(e0==1)
        {
            Fd_Entry* fd_ob=NULL;
            int e1=Fd_Table_find(w->fd_table,s.fd,&fd_ob);
            if(s.http!=NULL)
            {
                Http_analysis_free(s.http,w->http_pool);
                s.http=NULL;
            }
         
            if(e1!=1)
            {
                Memory_Queue_push(s.m_queue,s.size,s.char_ptr);

            }
            else
            {
                 if(fd_ob->send_tool->store==NULL)
                {
                    send_tool_alloc(fd_ob->send_tool,w->store_area,ed_store_blocknum);
                }

                int next=s.serial%fd_ob->send_tool->blocknum;
                    
                fd_ob->send_tool->store[next].use=1;
                fd_ob->send_tool->store[next].error_reason=s.error_reason;
                fd_ob->send_tool->store[next].m_queue=s.m_queue;
                fd_ob->send_tool->store[next].ptr=s.char_ptr;
                fd_ob->send_tool->store[next].size=s.size;
                    
                
                if(s.serial==fd_ob->ser_nex_send)
                {
                    send_fd=s.fd;
                }
            }
   
            
        }
        
    }
    
    

    if(send_fd==-2)
    {
        return 1;
    }

    Fd_Entry* fd_ob=NULL;
    Fd_Table_find(w->fd_table,send_fd,&fd_ob);
    int next=s.serial%fd_ob->send_tool->blocknum;

    int len=0;//之前说过用error_reason为正数时表示要发回的文本的长度
    len=fd_ob->send_tool->store[next].size;
    


    while(fd_ob->send_tool->store[next].use==1)
    {
        int n=write(send_fd,fd_ob->send_tool->store[next].ptr, len);
        if(n==0)
        {
            fd_close(w,send_fd);
        }
        Memory_Queue* m=fd_ob->send_tool->store[next].m_queue;
        int size=fd_ob->send_tool->store[next].size;
        char* ptr=fd_ob->send_tool->store[next].ptr;
        Memory_Queue_push(m,size,ptr);
        fd_ob->ser_nex_send++;
        fd_ob->send_tool->store[next].use=0;
        fd_ob->pack_in_path--;

        next=next+1%fd_ob->send_tool->blocknum;
    }


 
    if( fd_ob->ser_nex_send>= fd_ob->ser_fina_send)
    {
        send_tool_free(fd_ob->send_tool,w->store_area);
    }

    timer_alloc_and_reset(w->my_timer,s.fd,w);


    return 1;


}