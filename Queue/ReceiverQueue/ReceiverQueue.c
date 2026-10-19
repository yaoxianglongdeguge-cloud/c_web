#include "../../Include/Include.h"

static void ReceiverQueueEntryClear(Receiver_Queue_Entry* s){

    s->m_queue=NULL;
    s->fd=-1;
    s->serial=-1;
    s->error_reason=-1;
    s->char_ptr=NULL;
    s->size_resp=0;
    s->off=0;
    s->send_fd=0;
    s->size_file=0;
    s->offset=0;
    s->httpNeedFree=NULL;
}

int ReceiverQueueInit(Receiver_Queue** sq,int blocknum)
{
    if(sq==NULL||blocknum<=0) return -1;

    *sq=NULL;
    *sq=(Receiver_Queue*)malloc(sizeof(Receiver_Queue));
    if(*sq==NULL)  return -1;

    (*sq)->buf[0]=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry)*blocknum);
    (*sq)->buf[1]=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry)*blocknum);
    if((*sq)->buf[0]==NULL||(*sq)->buf[1]==NULL){
        free((*sq)->buf[0]);
        free((*sq)->buf[1]);
        free(*sq);
        *sq=NULL;
        return -1;
    }

    (*sq)->active=0;
    (*sq)->pending=1;
    (*sq)->active_num=0;
    (*sq)->pending_idx=0;
    (*sq)->pending_num=0;
    (*sq)->blocknum=blocknum;

    if(sem_init(&((*sq)->sem_thing_queue_notfull),0,(unsigned int)blocknum)!=0){
        free((*sq)->buf[0]);
        free((*sq)->buf[1]);
        free(*sq);
        *sq=NULL;
        return -1;
    }

    pthread_mutex_init(&((*sq)->mutex_thing),NULL);

    return 1;
}

int ReceiverQueueLen(Receiver_Queue* sq){

    if(sq==NULL) return 0;

    pthread_mutex_lock(&(sq->mutex_thing));
    int n=sq->active_num;
    pthread_mutex_unlock(&(sq->mutex_thing));

    //pending 计数是消费者线程私有的；本函数按约定也只由消费者线程调用
    n += sq->pending_num - sq->pending_idx;

    return n;
}

int ReceiverQueuePush(Receiver_Queue* sq,Worker_Queue* m_queue,int fd,int serial,int error_reason,
    int resp_size,int off,off_t file_size,char*char_ptr,int send_fd,off_t offset,void* httpNeedFree)
{
    if(sq==NULL) return 0;

    while(sem_wait(&(sq->sem_thing_queue_notfull))!=0){
        if(errno!=EINTR) return 0;
    }

    pthread_mutex_lock(&(sq->mutex_thing));

    Receiver_Queue_Entry* e=&(sq->buf[sq->active][sq->active_num]);
    e->m_queue=m_queue;
    e->error_reason=error_reason;
    e->fd=fd;
    e->size_file=file_size;
    e->size_resp=resp_size;
    e->send_fd=send_fd;
    e->char_ptr=char_ptr;
    e->serial=serial;
    e->offset=offset;
    e->off=off;
    e->httpNeedFree=httpNeedFree;

    sq->active_num++;

    pthread_mutex_unlock(&(sq->mutex_thing));

    return 1;
}

Receiver_Queue_Entry ReceiverQueueTopAndPop(Receiver_Queue* sq,int* error)//0为空
{
    Receiver_Queue_Entry s;
    ReceiverQueueEntryClear(&s);
    if(error!=NULL) *error=0;
    if(sq==NULL) return s;

    //1)先消费上一轮已经整批摘下来的 pending，不抢锁
    if(sq->pending_idx < sq->pending_num){

        s=sq->buf[sq->pending][sq->pending_idx];
        sq->pending_idx++;

        sem_post(&(sq->sem_thing_queue_notfull));
        if(error!=NULL) *error=1;
        return s;
    }

    //2)pending 空了：一次加锁，把 active 整缓冲“换”到 pending（O(1)）
    pthread_mutex_lock(&(sq->mutex_thing));

    if(sq->active_num==0){
        pthread_mutex_unlock(&(sq->mutex_thing));
        return s;
    }

    int old_active=sq->active;
    sq->active=sq->pending;          //旧 pending 已消费完，作为新的空 active
    sq->pending=old_active;

    sq->pending_num=sq->active_num;
    sq->pending_idx=0;
    sq->active_num=0;

    pthread_mutex_unlock(&(sq->mutex_thing));

    //3)消费刚摘下来的第一条
    s=sq->buf[sq->pending][sq->pending_idx];
    sq->pending_idx++;

    sem_post(&(sq->sem_thing_queue_notfull));
    if(error!=NULL) *error=1;
    return s;
}
