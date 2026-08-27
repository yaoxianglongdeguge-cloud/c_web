#include "../include.h"

int http_main(int fd,worker* worker,int ed_store_blocknum)
{

    Fd_Entry* fd_ob=NULL;
    int o0=Fd_Table_find(worker->fd_table,fd,&fd_ob);

    if(o0==0)
    {
        return 1;
    }

    if(fd_ob->pack_in_path>=ed_store_blocknum)
    {
        return 1;
    }
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
            fd_close(worker,fd,200);//对端关闭连接
            return 1;
        }
                   

    //状态机开始检索http请求
        int state=0;
        int error_reason=200;
        char* source_end=http_state_judge(fd_ob->http_store,&state,&error_reason);//要复制的文本结尾，此处在文本中
            
            
        if(state==-1&&fd_ob->http_store->ptr_e!=fd_ob->http_store->end)
        //不够一个请求的，
        //但是还不需要更大的存储区
        {
            int r1=Http_ed_store_write(fd_ob->http_store,fd);
            if(r1==-1)
            {
                if(fd_ob->http_store->ptr_b==fd_ob->http_store->ptr_e)
                //说明连接里没数据并且暂存区也没有残留数据,所以可以释放占有的暂存区
                {
                    Http_ed_store_free(fd_ob->http_store,worker->store_area);
                }
                    break;
                }
            else if(r1==0)//对面断开了连接
            {
                fd_close(worker,fd,200);
                return 1;
            }

        }
            
            
        else if(state==1)//解析出了一个完整请求
        {


                
            int h_size=5+(source_end-fd_ob->http_store->begin+1);//要复制的文本长度

            void* ptr=NULL;

            int notfull=0;
            Memory_Pool_alloc2(worker->http_pool,h_size/1024,&ptr,&notfull);
            if(notfull==0)
            {
                fd_close(worker,fd,503);
                return 1;
            }
            char* hptr=(char*)ptr;

            Http_ed_store_copy(fd_ob->http_store,source_end,hptr);
                        
                
            http_state_reset(fd_ob->http_store);    

            fd_ob->ser_fina_send++;
            
            worker_to_profession(worker,fd,hptr,h_size,error_reason,serial);
            fd_ob->pack_in_path++;

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

            worker_to_profession(worker,fd,NULL,0,error_reason,serial);
            fd_ob->pack_in_path++;
            error_reason=200;
                    
            break;
        }
                      
       
   }

                
    return 1;

}



