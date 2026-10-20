#include "../Include/Include.h"

int FdNextSendPut(Fd_Entry* fd_ob,Receiver_Queue_Entry* s){

    int next=s->serial%fd_ob->send_pointer_pool->blocknum;
                    
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

int GetAndJudge(Receiver_Thread* w,int fd,int edStoreBlockNum,int* send_info,int* sendFd){

    *send_info=0;
    Receiver_Queue_Entry s;
    int e0=1;
    while(e0!=0&&*sendFd==-2){

        s = ReceiverQueueTopAndPop(w->Thing_queue,&e0);
        if(e0!=1) continue ;

        MemoryPoolFree(w->http_pool,s.httpNeedFree);
        //获取连接对象
        *send_info=1;
        Fd_Entry* fd_ob=NULL;
        int e1=FdTableFind(w->fd_table,s.fd,&fd_ob);
        
        //如果该连接不存在，或者获取连接失败
        if(e1!=1||fd_ob==NULL){
            if(s.m_queue!=NULL){ //因为有可能是静态文件，本来就不用再给工作线程通知，所以可能也是NULL
                WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr);
            }
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

int MemoryQueuePushOperate(Fd_Entry* fd_ob,int next){

    Worker_Queue* m=fd_ob->send_pointer_pool->store[next].m_queue;
    int size_resp=fd_ob->send_pointer_pool->store[next].size_resp;
    char* ptr=fd_ob->send_pointer_pool->store[next].ptr;
    WorkerQueuePush(m,size_resp,ptr);
    fd_ob->send_pointer_pool->store[next].size_resp=0;
    fd_ob->send_pointer_pool->store[next].m_queue=NULL;
    fd_ob->send_pointer_pool->store[next].ptr=NULL;
    fd_ob->send_pointer_pool->store[next].off=0;

    return 1;
}

int ResponseSendMain(Receiver_Thread* w,int fd,int ed_store_blocknum,int* send_info)
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
    if(fd_ob == NULL) return 1;
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

            if(n==len){                       //① 全写完（最常见）

                if(fd_ob->need_wr==1){ EpollChangeRead(w->epfd,send_fd); fd_ob->need_wr=0; }
                MemoryQueuePushOperate(fd_ob,next);

            }
            else if(n>0){                     //② 部分写：没 EAGAIN，但也没写完

                fd_ob->send_pointer_pool->store[next].off=off1+n;

                if(fd_ob->need_wr==0){ EpollChangeReAndWr(w->epfd,send_fd); fd_ob->need_wr=1; }
                TimerReset(w->my_Timer,send_fd,w);

                return 1;
            }
            else if(n==0){                    //③ 对端关闭

                FdClose(w,send_fd,200);
                return 1;
            }
            else{                             //④ n==-1：按 errno 分流

                if(errno==EAGAIN||errno==EWOULDBLOCK){       //发送缓冲满

                    if(fd_ob->need_wr==0){ EpollChangeReAndWr(w->epfd,send_fd); fd_ob->need_wr=1; }
                    TimerReset(w->my_Timer,send_fd,w);

                    return 1;
                }
                if(errno==EINTR) return 1;                   //被信号打断：等 epoll 再触发

                FdClose(w,send_fd,200);                      //EPIPE/ECONNRESET/EBADF…
                return 1;
            }
        }
        
        //发送文件
        off_t size_file=fd_ob->send_pointer_pool->store[next].size_file;
        int send_ed_fd=fd_ob->send_pointer_pool->store[next].send_fd;
        if(size_file!=fd_ob->send_pointer_pool->store[next].offset){

            ssize_t sent = sendfile(send_fd,send_ed_fd, 
                &fd_ob->send_pointer_pool->store[next].offset,
                size_file-fd_ob->send_pointer_pool->store[next].offset);

            if(sent<0&&(errno==EPIPE||errno==ECONNRESET)){     //对端断开
                FdClose(w,send_fd,200);  close(send_ed_fd);  return 1;
            }
            if(sent<0&&errno==EINTR) return 1;                 //被信号打断：等 epoll 再触发
            if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){     //其它错误
                FdClose(w,send_fd,200);  close(send_ed_fd);  return 1;
            }

            //非阻塞下 sent==0 但文件还没到末尾：当成 would-block，不能当断开
            if(sent==0&&fd_ob->send_pointer_pool->store[next].offset<size_file){

                if(fd_ob->need_wr==0){ EpollChangeReAndWr(w->epfd,send_fd); fd_ob->need_wr=1; }
                TimerReset(w->my_Timer,send_fd,w);

                return 1;
            }

            if(fd_ob->send_pointer_pool->store[next].offset<size_file){   //文件没写完

                if(fd_ob->need_wr==0){ EpollChangeReAndWr(w->epfd,send_fd); fd_ob->need_wr=1; }
                TimerReset(w->my_Timer,send_fd,w);

                return 1;
            }

            //文件写完
            if(fd_ob->need_wr==1){ EpollChangeRead(w->epfd,send_fd); fd_ob->need_wr=0; }
            close(send_ed_fd);
        }

        fd_ob->ser_nex_send++;
        fd_ob->send_pointer_pool->store[next].use=0;
        fd_ob->pack_in_path--;

        next=(next+1)%fd_ob->send_pointer_pool->blocknum;
    }




 //暂存池里没有包了.可以回收暂存池
    if( fd_ob->ser_nex_send>= fd_ob->ser_fina_send) SendPointerPoolFree(fd_ob->send_pointer_pool,w->store_area);

    TimerReset(w->my_Timer,send_fd,w);

    //一组包发完了：如果这个连接还有"因在途窗口满而暂存"的请求，就续着处理
    //（否则那些已经读进暂存区的流水线请求会一直没人投递 → 客户端卡住）
    if(fd_ob->pending==1&&fd_ob->pack_in_path<ed_store_blocknum){
        fd_ob->pending=0;
        RequestComeMain(send_fd,w,ed_store_blocknum);
    }

    return 1;


}