#include "../Include/Include.h"
#include <sys/eventfd.h>


int ReceiverThreadInit(Receiver_Thread** w,int Listen_fd,int id,int workersNum)
{

    *w=(Receiver_Thread*)malloc(sizeof(Receiver_Thread));
    if(*w==NULL){
        fprintf(stderr,"[Recv] 组%d malloc Receiver_Thread 失败\n",id);
        return 0;
    }


    MemoryPoolInit(&((*w)->http_pool),g_cfg.pool_recv_http_kb*1024,1);

    MemoryPoolInit(&((*w)->store_area),g_cfg.pool_recv_store_kb*1024,1);
    ReceiverQueueInit(&((*w)->Thing_queue));
    ReceiverQueueTempInit(&((*w)->sendTemp));

    //任务队列改成接待线程私有：每个组一条，组内的工作线程和替补工作线程都取这一条
    TasKQueueInit(&((*w)->taskQueue),g_cfg.task_queue_capacity);

    TimerInit(&((*w)->my_Timer),g_cfg.timer_capacity);
    FdTableInit(&((*w)->fd_table),g_cfg.fd_table_init_level);


    (*w)->id = id;
    (*w)->notified = 0;
    (*w)->isFull = 0;

    (*w)->epfd = epoll_create1(0);
    if((*w)->epfd < 0){
        fprintf(stderr,"[Recv] 组%d epoll_create1 失败：errno=%d (%s)\n",id,errno,strerror(errno));
        return 0;
    }
    EpollReadMount((*w)->epfd,Listen_fd);

    (*w)->wakeup_fd=eventfd(0,EFD_NONBLOCK);
    if((*w)->wakeup_fd < 0){
        fprintf(stderr,"[Recv] 组%d eventfd 失败：errno=%d (%s)（内核需 >=2.6.27）\n",
                id,errno,strerror(errno));
        return 0;
    }
    EpollReadMount((*w)->epfd,(*w)->wakeup_fd);

    if(TempPackInit(&((*w)->tempPack),g_cfg.temp_pack_capacity)!=1){
        fprintf(stderr,"[Recv] 组%d TempPackInit 失败（temp_pack.capacity=%d）\n",
                id,g_cfg.temp_pack_capacity);
        return 0;
    }

    if(ThreadPoolInit((*w),workersNum)!=1){
        fprintf(stderr,"[Recv] 组%d 线程池初始化失败（worker.per_group=%d，要求 >=2）\n",
                id,workersNum);
        return 0;
    }

    printf("[Recv] 组%d 就绪：epfd=%d wakeup_fd=%d worker=%d\n",
           id,(*w)->epfd,(*w)->wakeup_fd,workersNum);
    fflush(stdout);

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

int ReceiverToWorker(Receiver_Thread* w,int fd,uint64_t onlyId,char* h,int h_size,int error_reason,int serial)
{
    //投到本组接待线程自己的任务队列，不再投全局队列
    int e0=TasKQueuePush(w->taskQueue,w,fd,onlyId,serial,error_reason,h,h_size);
    if(e0!=1){
        if(TempPackPush(w->tempPack,fd,onlyId,h,h_size,error_reason,serial)!=1) return -1;   //环也满
        return 0;                                                                     //先放环里，下次再投
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
            n=epoll_wait(w->epfd,events,1024,g_cfg.epoll_tick_ms);  
            if(n<0&&errno!=EINTR){
                static int err_logged=0;
                if(err_logged<3){
                    err_logged++;
                    fprintf(stderr,"[Recv] 组%d epoll_wait 失败：errno=%d (%s)（epoll.tick_ms=%d）\n",
                            w->id,errno,strerror(errno),g_cfg.epoll_tick_ms);
                }
            }
            Thread_Pool* t = w->threadPool;
            if(t==NULL){
                fprintf(stderr,"[Recv] 组%d threadPool 为空，事件循环退出（检查 worker.per_group）\n",w->id);
                return 0;
            }
            Worker_Thread* main = t->workers[t->mainThread];

            // 结束时间在开始时间前面说明在业务中没出来，如果是在任务队列那里阻塞的话，那开始时间是在结束时间前面的
            // 所以不会误触发换线程
            if(main->taskStart != 0 && main->taskEnd <= main->taskStart){
       
                if(now_ns() - main->taskStart >= g_cfg.worker_stall_ms*1000000){
          
                    int isSuccess = 0;
                    ThreadPoolChange(w,&isSuccess);
                }
            }
    
            for(int i=0;i<g_cfg.recv_send_batch;i++)
            {
                int send_info=0;
                int e2=ResponseSendMain(w,-2,ed_store_blocknum,&send_info);
                if(send_info==0)  break;
            
            }

            if(n>0&&w->tempPack->count==0)  break;
            else{
                //把积压的请求尽量投递出去；队列还满就下次再试（失败的那条已由 ReceiverToWorker 放回环里）
                int fd_t; uint64_t oi_t; char* h_t; int hs_t, er_t, se_t;
                while(TempPackPop(w->tempPack,&fd_t,&oi_t,&h_t,&hs_t,&er_t,&se_t)==1){
                    int put = ReceiverToWorker(w,fd_t,oi_t,h_t,hs_t,er_t,se_t);
                    if(put == 1) continue;
                    break;
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
                if(ReceiverQueueLen(w->Thing_queue)>0&&
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

                Fd_Entry* fd_ob=NULL;
                if(FdTableFind(w->fd_table,handle_fd,&fd_ob)==1&&fd_ob!=NULL){
                    TimerReset(w->my_Timer,handle_fd,fd_ob->onlyId,w);
                }
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
