#include "../include.h"

int FdNextSendPut(Fd_Entry* fd_ob,Send_tq_Entry* s){

    int next=s.serial%fd_ob->send_pointer_pool->blocknum;
                    
    fd_ob->send_pointer_pool->store[next].use=1;
    fd_ob->send_pointer_pool->store[next].error_reason=s->error_reason;
    fd_ob->send_pointer_pool->store[next].m_queue=s->m_queue;
    fd_ob->send_pointer_pool->store[next].ptr=s->char_ptr;
    fd_ob->send_pointer_pool->store[next].size_file=s->size_file;
    fd_ob->send_pointer_pool->store[next].send_fd=s->send_fd;
    fd_ob->send_pointer_pool->store[next].size_resp=s->size_resp;
    fd_ob->send_pointer_pool->store[next].offset=s->offset;
    fd_ob->send_pointer_pool->store[next].off=s->off;
    
    return 1;
}

int GetAndJudge(worker* w,int fd,int edStoreBlockNum,int* send_info,int* sendFd){

    *send_info=0;
    Send_tq_Entry s;
    while(e0!=0&&send_fd==-2){
        
        int e0=1;
        s = Send_thing_queue_top_and_pop(w->Thing_queue,&e0);
        if(e0!=1) continue ;

        //获取连接对象
        *send_info=1;
        Fd_Entry* fd_ob=NULL;
        int e1=FdTableFind(w->fd_table,s.fd,&fd_ob);
                
        //如果该连接不存在，或者获取连接失败
        if(e1!=1&&s.m_queue!=NULL){
            Memory_Queue_push(s.m_queue,s.size_resp,s.char_ptr);
            continue;
        }

        //如果连接存在但是没有send暂存池
        if(fd_ob->send_pointer_pool->store==NULL){
            int n=SendPointerPoolAlloc(fd_ob->send_pointer_pool,w->store_area,edStoreBlockNum);
            if(n!=1){ FdClose(w,fd_ob->fd,503); return 0; }
        }
            
        //把要发的包信息存在send指针暂存池
        FdNextSendPut(fd_ob,&s);
                    
        //如果本连接下一个要发的包来了，那就开始发送
        if(s.serial==fd_ob->ser_nex_send) *sendFd=s.fd;
            
    }

    return 1;

}

int MemoryQueuePushOperate(Fd_Entry* fd_ob){

    Memory_Queue* m=fd_ob->send_pointer_pool->store[next].m_queue;
    int size_resp=fd_ob->send_pointer_pool->store[next].size_resp;
    char* ptr=fd_ob->send_pointer_pool->store[next].ptr;
    Memory_Queue_push(m,size_resp,ptr);
    fd_ob->send_pointer_pool->store[next].size_resp=0;
    fd_ob->send_pointer_pool->store[next].m_queue=NULL;
    fd_ob->send_pointer_pool->store[next].ptr=NULL;
    fd_ob->send_pointer_pool->store[next].off=0;

    return 1;
}

int ResponseSendMain(worker* w,int fd,int ed_store_blocknum,int* send_info)
{
    int send_fd=-2;
    
    //如果fd不等于-2说明是之前没写完的连接能继续写了，所以不需要在任务队列中再取事件
    if(fd!=-2) send_fd=fd;
    else {
        //取事件并判断下一个要发送谁
        int back = GetAndJudge(w,fd,ed_store_blocknum,send_info,&send_fd) ;    
        if(back == 0){ return 1;}
    }
        
    if(send_fd==-2) return 1;

    //开始发包
    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,send_fd,&fd_ob);
    int next=fd_ob->ser_nex_send%fd_ob->send_pointer_pool->blocknum;

    while(fd_ob->send_pointer_pool->store[next].use==1)
    {
        int len=0;//之前说过用error_reason为正数时表示要发回的文本的长度
        int off1=fd_ob->send_pointer_pool->store[next].off;
        len=fd_ob->send_pointer_pool->store[next].size_resp-off1;

        if(fd_ob->send_pointer_pool->store[next].error_reason!=200){
            FdClose(w,send_fd,fd_ob->send_pointer_pool->store[next].error_reason); return 1; }
        
        //发送返回头部
        if(fd_ob->send_pointer_pool->store[next].size_resp!=fd_ob->send_pointer_pool->store[next].off){

            int n=write(send_fd,fd_ob->send_pointer_pool->store[next].ptr+off1, len);
            if(n==0){  FdClose(w,send_fd,200);  return 1;  }
            else if(n<len&&n>0){

                off1=off1+n;
                fd_ob->send_pointer_pool->store[next].off=off1;    

                EpollChangeReAndWr(w->epfd,send_fd);      
                TimerReset(w->my_Timer,send_fd,w);

                return 1;
            } else if(n==-1&&errno==EAGAIN) {
                
                EpollChangeReAndWr(w->epfd,send_fd);       
                TimerReset(w->my_Timer,send_fd,w);

                return 1;
            } else if(n>=len) {
                
                EpollChangeRead(w->epfd,send_fd);    
                MemoryQueuePushOperate(fd_ob);

            }
        }
        
        //发送文件
        off_t size_file=fd_ob->send_pointer_pool->store[next].size_file;
        int send_ed_fd=fd_ob->send_pointer_pool->store[next].send_fd;
        if(size_file!=fd_ob->send_pointer_pool->store[next].offset){

            ssize_t sent = sendfile(send_fd,send_ed_fd, 
                &fd_ob->send_pointer_pool->store[next].offset,
                size_file-fd_ob->send_pointer_pool->store[next].offset);
            // 对端断开，关闭连接
            if (sent < 0 && errno == EPIPE) {  FdClose(w,send_fd,200);  close(send_ed_fd);  return 1;  }
            if (sent==0) {  FdClose(w,send_fd,200);  close(send_ed_fd);  return 1;  }

            if(size_file!=fd_ob->send_pointer_pool->store[next].offset){//文件没写完

                if(sent>0){

                    EpollChangeReAndWr(w->epfd,send_fd);
                    TimerReset(w->my_Timer,send_fd,w);
                    return 1;
                }
                else if (sent<0 && errno == EAGAIN) {
                    EpollChangeReAndWr(w->epfd,send_fd);
                    TimerReset(w->my_Timer,send_fd,w);
                    return 1;
                }
            }else{//文件写完了
                EpollChangeRead(w->epfd,send_fd);  
                close(send_ed_fd);
            }
            
        }


        fd_ob->ser_nex_send++;
        fd_ob->send_pointer_pool->store[next].use=0;
        fd_ob->pack_in_path--;

        next=(next+1)%fd_ob->send_pointer_pool->blocknum;
    }




 //暂存池里没有包了.可以回收暂存池
    if( fd_ob->ser_nex_send>= fd_ob->ser_fina_send) SendPointerPoolFree(fd_ob->send_pointer_pool,w->store_area);

    TimerReset(w->my_Timer,send_fd,w);

    return 1;


}