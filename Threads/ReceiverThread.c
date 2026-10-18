#include "../Include/Include.h"
#include <sys/eventfd.h>   /* 【延迟修复】eventfd 唤醒通道 */

int TempPackInit(Temp_Pack** tempPack){

    (*tempPack)=malloc(sizeof(Temp_Pack));
    (*tempPack)->error_reason=-1;
    (*tempPack)->h=NULL;
    (*tempPack)->h_size=0;
    (*tempPack)->had=false;
    (*tempPack)->serial=-1;

    return 1;
}

int TempPackPut(Temp_Pack* tempPack,bool had,int fd,char* h,int h_size,int error_reason,int serial){
    tempPack->error_reason=error_reason;
    tempPack->fd=fd;
    tempPack->h=h;
    tempPack->h_size=h_size;
    tempPack->had=had;
    tempPack->serial=serial;

    return 1;
}

int ReceiverThreadInit(Receiver_Thread** w,int Listen_fd,int id)
{

    *w=(Receiver_Thread*)malloc(sizeof(Receiver_Thread));

    MemoryPoolInit(&((*w)->http_pool),2048*1024,8);
    MemoryPoolInit(&((*w)->store_area),2048*1024,8);
    ReceiverQueueInit(&((*w)->Thing_queue),100);
    TimerInit(&((*w)->my_Timer),5000);
    FdTableInit(&((*w)->fd_table),4);


    (*w)->epfd=epoll_create1(0);
    (*w)->id=id;
    (*w)->notified=0;
    EpollReadMount((*w)->epfd,Listen_fd);

    (*w)->wakeup_fd=eventfd(0,EFD_NONBLOCK);
    EpollReadMount((*w)->epfd,(*w)->wakeup_fd);

    TempPackInit(&((*w)->tempPack));

    return 1;

}

int ReceiverNotify(Receiver_Thread* w)
{
    if(w!=NULL&&w->wakeup_fd>=0){
        //【唤醒合并】只有从"未通知"变成"已通知"时才真写一次；接待线程处理完会清 0
        if(__atomic_exchange_n(&w->notified,1,__ATOMIC_ACQ_REL)==0){
            uint64_t one=1;
            ssize_t n=write(w->wakeup_fd,&one,sizeof(one));
            (void)n;
        }
    }
    return 1;
}

int ReceiverToWorker(Receiver_Thread* w,int fd,char* h,int h_size,int error_reason,int serial)
{
    int e0=TasKQueuePush(taskQueue,w,fd,serial,error_reason,h,h_size);
    if(e0!=1){
        TempPackPut(w->tempPack,true,fd,h,h_size,error_reason,serial);
        return 0;
    }
    return 1;
}

int ReveiverThreadMain(Receiver_Thread* w,int Listen_fd,int time,int ed_store_blocknum)
{
    
    struct epoll_event events[1024];
    while(1)
    {
        int n=-1;
        while(1){
            n=epoll_wait(w->epfd,events,1024,10);  
            for(int i=0;i<100;i++)
            {
                int send_info=0;
                int e2=ResponseSendMain(w,-2,ed_store_blocknum,&send_info);
                if(send_info==0)  break;
            
            }

            if(n>0&&w->tempPack->had==false)  break;
            else{
                if(w->tempPack->had==true){
                    int put = ReceiverToWorker(w,w->tempPack->fd,w->tempPack->h,w->tempPack->h_size,
                        w->tempPack->error_reason,w->tempPack->serial);
                    if(put == 1) TempPackPut(w->tempPack,false,-1,NULL,0,-1,-1);
                }
            }
            TimerOvertime(w->my_Timer,time,w);
        }
        for(int i=0;i<n;i++){

            int handle_fd=events[i].data.fd;
            if(handle_fd<3)  continue;


            if(handle_fd==w->wakeup_fd){
                uint64_t drain;
                while(read(w->wakeup_fd,&drain,sizeof(drain))>0){}
                __atomic_store_n(&w->notified,0,__ATOMIC_RELEASE);

                //清完标志后再看一眼响应队列：还有积压就补一次唤醒，避免丢唤醒
                if(w->Thing_queue!=NULL&&w->Thing_queue->num>0&&
                   __atomic_exchange_n(&w->notified,1,__ATOMIC_ACQ_REL)==0){
                    uint64_t one=1;
                    ssize_t n=write(w->wakeup_fd,&one,sizeof(one));
                    (void)n;
                }
                continue;
            }

            if(handle_fd==Listen_fd) 
            { int e0=FdConnect(w,handle_fd); }
            else{

                TimerReset(w->my_Timer,handle_fd,w);
                if(events[i].events & EPOLLIN){
                    int e1=RequestComeMain(handle_fd,w,ed_store_blocknum);//错误包已经通过发送程序发了，所以这里的返回值不验证
                }
                if(events[i].events & EPOLLOUT){
                    int send_info=1;
                    ResponseSendMain(w,handle_fd,ed_store_blocknum,&send_info);
                }
                
            }


        }
 
    }
    
    return 1;
}
