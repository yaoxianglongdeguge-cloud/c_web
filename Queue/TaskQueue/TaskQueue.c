#include "../../Include/Include.h"

static void TaskQueueEntryClear(Task_Queue_Entry* e){

    e->w=NULL;
    e->fd=-1;
    e->onlyId=0;
    e->serial=-1;
    e->error_reason=-1;
    e->http=NULL;
    e->h_size=0;
}

int TasKQueueInit(Task_Queue** sq,int blocknum)
{
    if(sq==NULL||blocknum<=0) return -1;

    *sq=NULL;
    *sq=(Task_Queue*)malloc(sizeof(Task_Queue));
    if(*sq==NULL)  return -1;


    (*sq)->buf[0]=(Task_Queue_Entry*)malloc(sizeof(Task_Queue_Entry)*blocknum);
    (*sq)->buf[1]=(Task_Queue_Entry*)malloc(sizeof(Task_Queue_Entry)*blocknum);
    if((*sq)->buf[0]==NULL||(*sq)->buf[1]==NULL){
        free((*sq)->buf[0]);
        free((*sq)->buf[1]);
        free(*sq);
        *sq=NULL;
        return -1;
    }

    for(int i=0;i<blocknum;i++){
        TaskQueueEntryClear(&((*sq)->buf[0][i]));
        TaskQueueEntryClear(&((*sq)->buf[1][i]));
    }

    (*sq)->active=0;
    (*sq)->pending=1;
    (*sq)->active_num=0;
    (*sq)->pending_num=0;
    (*sq)->pending_idx=0;
    (*sq)->pending_reader=0;
    (*sq)->swapping=0;
    (*sq)->num=0;
    (*sq)->blocknum=blocknum;

    pthread_mutex_init(&((*sq)->mutex_task),NULL);
    sem_init(&((*sq)->sem_task_notempty),0,0);

    return 1;
}

int TasKQueuePush(Task_Queue* sq,Receiver_Thread* w,int fd,uint64_t onlyId,int serial,int error_reason,char* h,int h_size)
{
    if(sq==NULL) return 0;

    //生产者只锁 active：消费者在 pending 里取任务，不用同一把锁
    pthread_mutex_lock(&(sq->mutex_task));

    if(__atomic_load_n(&(sq->num),__ATOMIC_SEQ_CST)>=sq->blocknum){
        pthread_mutex_unlock(&(sq->mutex_task));
        return 0;
    }

    Task_Queue_Entry* e=&(sq->buf[sq->active][sq->active_num]);
    e->w=w;
    e->error_reason=error_reason;
    e->http=h;
    e->fd=fd;
    e->onlyId=onlyId;
    e->serial=serial;
    e->h_size=h_size;

    sq->active_num++;
    __atomic_add_fetch(&(sq->num),1,__ATOMIC_SEQ_CST);

    pthread_mutex_unlock(&(sq->mutex_task));
    sem_post(&(sq->sem_task_notempty));

    return 1;
}

Task_Queue_Entry TasKQueueTopAndPop(Task_Queue* sq,int* error)
{
    Task_Queue_Entry s;
    TaskQueueEntryClear(&s);

    if(error!=NULL) *error=0;
    if(sq==NULL) return s;

    for(int i=0;i<2;i++){

        //1) 先登记“我在读 pending”：换缓冲的人要等读者全部退出，
        //   才把这块旧缓冲交回给生产者写，否则会把正在拷贝的条目覆盖掉
        __atomic_add_fetch(&(sq->pending_reader),1,__ATOMIC_SEQ_CST);

        if(__atomic_load_n(&(sq->swapping),__ATOMIC_SEQ_CST)!=0){
            __atomic_sub_fetch(&(sq->pending_reader),1,__ATOMIC_SEQ_CST);
            return s;                          //正在换缓冲：本次返回空，调用者稍后再来
        }

        //空队列被反复轮询时不要一直自增 pending_idx（长跑会溢出），
        //先看一眼有没有未认领的，没有就直接去换缓冲
        int idx=-1;
        int num=__atomic_load_n(&(sq->pending_num),__ATOMIC_SEQ_CST);

        if(__atomic_load_n(&(sq->pending_idx),__ATOMIC_SEQ_CST)<num){
            idx=__atomic_fetch_add(&(sq->pending_idx),1,__ATOMIC_SEQ_CST);
            //防止被换缓冲，所以这里再确认一下，其实上面已经把要换缓冲的挡住了，如果有的话那本次就进不来这里
            num=__atomic_load_n(&(sq->pending_num),__ATOMIC_SEQ_CST);
        }

        if(idx>=0&&idx<num){

            //pending 在“全体认领完 + 读者归零”之前不会被换走，这里可以放心按号取
            s=sq->buf[__atomic_load_n(&(sq->pending),__ATOMIC_SEQ_CST)][idx];
            __atomic_sub_fetch(&(sq->pending_reader),1,__ATOMIC_SEQ_CST);

            //虽然这里可能发生换缓存，但是num是记录的两条缓存的总有效个数，所以直接原子减没问题
            __atomic_sub_fetch(&(sq->num),1,__ATOMIC_SEQ_CST);
            while(sem_trywait(&(sq->sem_task_notempty))!=0){
                if(errno!=EINTR) break;        //信号量只用来唤醒，取不到也不影响本次取任务
            }

            if(error!=NULL) *error=1;
            return s;
        }

        __atomic_sub_fetch(&(sq->pending_reader),1,__ATOMIC_SEQ_CST);

        //2) pending 已经取空：锁内把 active 整块换过来（只交换指针和计数，O(1)）
        //   条件必须是“全部被认领（pending_idx>=pending_num）且没有读者”，
        //   否则旧 pending 里还有没取走的任务，换了就丢了
        int swapped=0;

        pthread_mutex_lock(&(sq->mutex_task));

        int pidx=__atomic_load_n(&(sq->pending_idx),__ATOMIC_SEQ_CST);
        int pnum=__atomic_load_n(&(sq->pending_num),__ATOMIC_SEQ_CST);

        if(pidx>=pnum&&sq->active_num>0){

            __atomic_store_n(&(sq->swapping),1,__ATOMIC_SEQ_CST);

            if(__atomic_load_n(&(sq->pending_reader),__ATOMIC_SEQ_CST)==0){

                int old=sq->pending;
                __atomic_store_n(&(sq->pending),sq->active,__ATOMIC_SEQ_CST);
                sq->active=old;

                __atomic_store_n(&(sq->pending_num),sq->active_num,__ATOMIC_SEQ_CST);
                __atomic_store_n(&(sq->pending_idx),0,__ATOMIC_SEQ_CST);
                sq->active_num=0;

                swapped=1;
            }

            __atomic_store_n(&(sq->swapping),0,__ATOMIC_SEQ_CST);
        }

        pthread_mutex_unlock(&(sq->mutex_task));

        if(swapped!=1) return s;               //真没货（或这次没换成），非阻塞返回

        //3) 换成功了：回到循环开头，从新的 pending 里认领第一条
    }

    return s;
}







