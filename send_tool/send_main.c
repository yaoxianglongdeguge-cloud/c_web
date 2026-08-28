#include "../include.h"


int send_main(worker* w,int fd,int ed_store_blocknum,int* send_info)
{
    int send_fd=-2;
    //这里是看返回事件队列里能现在立刻发的包，然后顺便发他后面顺延的包
    
    if(fd!=-2)
    {
        send_fd=fd;
    }
    else
    {
        
        *send_info=0;
        Send_tq_Entry s;
        int e0=1;
        while(e0!=0&&send_fd==-2)
        {
            s = Send_thing_queue_top_and_pop(w->Thing_queue,&e0);
            if(e0==1)
            {
                *send_info=1;
                Fd_Entry* fd_ob=NULL;
                int e1=Fd_Table_find(w->fd_table,s.fd,&fd_ob);
                
                if(e1!=1)
                {
                    if(s.m_queue!=NULL)
                    {
                        Memory_Queue_push(s.m_queue,s.size_resp,s.char_ptr);
                    }
                    
                }
                else
                {
                    if(fd_ob->send_tool->store==NULL)
                    {
                        int n=send_tool_alloc(fd_ob->send_tool,w->store_area,ed_store_blocknum);
                        if(n!=1)
                        {
                            fd_close(w,fd_ob->fd,503);
                            return 1;
                        }
                    }
                    
                    int next=s.serial%fd_ob->send_tool->blocknum;
                    
                    fd_ob->send_tool->store[next].use=1;
                    fd_ob->send_tool->store[next].error_reason=s.error_reason;
                    fd_ob->send_tool->store[next].m_queue=s.m_queue;
                    fd_ob->send_tool->store[next].ptr=s.char_ptr;
                    fd_ob->send_tool->store[next].size_file=s.size_file;
                    fd_ob->send_tool->store[next].send_fd=s.send_fd;
                    fd_ob->send_tool->store[next].size_resp=s.size_resp;
                    fd_ob->send_tool->store[next].offset=s.offset;
                    fd_ob->send_tool->store[next].off=s.off;
                    
                    
                    if(s.serial==fd_ob->ser_nex_send)
                    {
                        send_fd=s.fd;
                    }
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
    int next=fd_ob->ser_nex_send%fd_ob->send_tool->blocknum;

    int len=0;//之前说过用error_reason为正数时表示要发回的文本的长度
    len=fd_ob->send_tool->store[next].size_resp;
    

    while(fd_ob->send_tool->store[next].use==1)
    {
        int len=0;//之前说过用error_reason为正数时表示要发回的文本的长度
        int off1=fd_ob->send_tool->store[next].off;
        len=fd_ob->send_tool->store[next].size_resp-off1;

        if(fd_ob->send_tool->store[next].error_reason!=200)
        {
            int error_reason=fd_ob->send_tool->store[next].error_reason;
            fd_close(w,send_fd,error_reason);
            return 1;
        }
        
        if(fd_ob->send_tool->store[next].size_resp!=fd_ob->send_tool->store[next].off)
        {

            int n=write(send_fd,fd_ob->send_tool->store[next].ptr+off1, len);
            if(n==0)
            {
                fd_close(w,send_fd,200);
                return 1;
            }
            else if(n<len&&n>0)
            {
                off1=off1+n;
                fd_ob->send_tool->store[next].off=off1;    


                struct epoll_event ev;
                ev.events  = EPOLLIN | EPOLLOUT;   
                ev.data.fd = send_fd;             
                epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev);        


                timer_alloc_and_reset(w->my_timer,send_fd,w);
                return 1;
            }
            else if(n==-1&&errno==EAGAIN)
            {
                
                struct epoll_event ev;
                ev.events  = EPOLLIN | EPOLLOUT;   
                ev.data.fd = send_fd;             
                epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev);        

                timer_alloc_and_reset(w->my_timer,send_fd,w);
                return 1;
            }
            else if(n>=len)
            {
                
                struct epoll_event ev;
                ev.events  = EPOLLIN;
                ev.data.fd = send_fd;             
                epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev);        

                Memory_Queue* m=fd_ob->send_tool->store[next].m_queue;
                int size_resp=fd_ob->send_tool->store[next].size_resp;
                char* ptr=fd_ob->send_tool->store[next].ptr;
                Memory_Queue_push(m,size_resp,ptr);
                fd_ob->send_tool->store[next].size_resp=0;
                fd_ob->send_tool->store[next].m_queue=NULL;
                fd_ob->send_tool->store[next].ptr=NULL;
                fd_ob->send_tool->store[next].off=0;

            }
        }//这里是处理请求头和在内存中的请求体,由于内存中的回复规定小于1mb，所以写缓存区可以装下

        
        off_t size_file=fd_ob->send_tool->store[next].size_file;
        int send_ed_fd=fd_ob->send_tool->store[next].send_fd;
        if(size_file!=fd_ob->send_tool->store[next].offset)
        {
            ssize_t sent = sendfile(send_fd,send_ed_fd, &fd_ob->send_tool->store[next].offset,size_file-fd_ob->send_tool->store[next].offset);
            if (sent < 0 && errno == EPIPE) {
            // 对端断开，关闭连接
                fd_close(w,send_fd,200);
                close(send_ed_fd);
                return 1;
            }
            if (sent==0)
            {
                    fd_close(w,send_fd,200);
                    close(send_ed_fd);
                    return 1;
            }
            if(size_file!=fd_ob->send_tool->store[next].offset)
            {

                if(sent>0)
                {
                   struct epoll_event ev;
                   ev.events  = EPOLLIN | EPOLLOUT;   
                   ev.data.fd = send_fd;             
                   epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev); 

                    timer_alloc_and_reset(w->my_timer,send_fd,w);
                    return 1;
                }
                else if (sent<0 && errno == EAGAIN) {

                    struct epoll_event ev;
                   ev.events  = EPOLLIN | EPOLLOUT;   
                   ev.data.fd = send_fd;             
                   epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev); 
                    
                    timer_alloc_and_reset(w->my_timer,send_fd,w);
                    return 1;
                }
            }
            else
            {
                struct epoll_event ev;
                ev.events  = EPOLLIN;   
                ev.data.fd = send_fd;             
                epoll_ctl(w->epfd, EPOLL_CTL_MOD, send_fd, &ev); 

                close(send_ed_fd);
            }
            
        }//这里处理文件



        fd_ob->ser_nex_send++;
        fd_ob->send_tool->store[next].use=0;
        fd_ob->pack_in_path--;

        next=(next+1)%fd_ob->send_tool->blocknum;
    }




 
    if( fd_ob->ser_nex_send>= fd_ob->ser_fina_send)
    {
        send_tool_free(fd_ob->send_tool,w->store_area);
    }

    timer_alloc_and_reset(w->my_timer,send_fd,w);


    return 1;


}