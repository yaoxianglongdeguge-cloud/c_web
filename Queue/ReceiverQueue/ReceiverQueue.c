#include "../../Include/Include.h"

static void ReceiverQueueEntryClear(Receiver_Queue_Entry* s){

    s->m_queue=NULL;
    s->fd=-1;
    s->onlyId=0;
    s->serial=-1;
    s->error_reason=-1;
    s->char_ptr=NULL;
    s->size_resp=0;
    s->off=0;
    s->send_fd=0;
    s->size_file=0;
    s->offset=0;
    s->httpNeedFree=NULL;
    s->next=NULL;
    s->pre=NULL;
}

void ReceiverQueueEntryCopy(Receiver_Queue_Entry* s1,Receiver_Queue_Entry* s2){
    if(s1 == NULL || s2 == NULL) return ;

    s1->char_ptr = s2->char_ptr;
    s1->error_reason = s2->error_reason;
    s1->fd = s2->fd;
    s1->onlyId = s2->onlyId;
    s1->m_queue = s2->m_queue;
    s1->off = s2->off;
    s1->offset = s2->offset;
    s1->send_fd = s2->send_fd;
    s1->serial = s2->serial;
    s1->size_file = s2->size_file;
    s1->size_resp = s2->size_resp;
    
    s1->httpNeedFree = s2->httpNeedFree;
    s1->next = s2->next;
    s1->pre = s2->pre;
}

int ReceiverQueueInit(Receiver_Queue** sq)
{
    if(sq==NULL) return -1;

    *sq=NULL;
    *sq=(Receiver_Queue*)malloc(sizeof(Receiver_Queue));
    if(*sq==NULL)  return -1;

    (*sq)->head=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry));
    (*sq)->tail=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry));
    if((*sq)->head==NULL||(*sq)->tail==NULL){
        free((*sq)->head);
        free((*sq)->tail);
        free(*sq);
        *sq=NULL;
        return -1;
    }

    ReceiverQueueEntryClear((*sq)->head);
    ReceiverQueueEntryClear((*sq)->tail);
    (*sq)->head->next = (*sq)->tail;
    (*sq)->tail->pre = (*sq)->head;

    (*sq)->hadNum = 0;

    pthread_mutex_init(&((*sq)->mutex),NULL);

    return 1;
}

int ReceiverQueueLen(Receiver_Queue* sq)
{
    if(sq==NULL) return 0;

    pthread_mutex_lock(&(sq->mutex));
    int n=sq->hadNum;
    pthread_mutex_unlock(&(sq->mutex));

    return n;
}

int ReceiverQueuePush(Receiver_Queue* sq,Worker_Queue* m_queue,int fd,uint64_t onlyId,int serial,int error_reason,
    int resp_size,int off,off_t file_size,char*char_ptr,int send_fd,off_t offset,void* httpNeedFree,int httpSize)
{

    if(sq==NULL||httpNeedFree==NULL||httpSize<0) return 0;

    Receiver_Queue_Entry* e = (Receiver_Queue_Entry*)(httpNeedFree + RECEIVER_QUEUE_NODE_OFFSET(httpSize));
    e->httpNeedFree=httpNeedFree;             

    pthread_mutex_lock(&(sq->mutex));

    e->m_queue=m_queue;
    e->error_reason=error_reason;
    e->fd=fd;
    e->onlyId=onlyId;
    e->size_file=file_size;
    e->size_resp=resp_size;
    e->send_fd=send_fd;
    e->char_ptr=char_ptr;
    e->serial=serial;
    e->offset=offset;
    e->off=off;

    e->next = sq->tail;
    e->pre = sq->tail->pre;
    sq->tail->pre->next = e;
    sq->tail->pre = e;

    sq->hadNum++;
    pthread_mutex_unlock(&(sq->mutex));

    
    return 1;
}

Receiver_Queue_Entry* ReceiverQueueTopAndPop(Receiver_Queue* sq,int* error)//0为空
{

     if(error!=NULL) *error=0;
    if(sq==NULL) return NULL;

    pthread_mutex_lock(&(sq->mutex));

    Receiver_Queue_Entry* first = sq->head->next;
    if(first == sq->tail){
        pthread_mutex_unlock(&(sq->mutex));
        return NULL;
    }

    Receiver_Queue_Entry* last = sq->tail->pre;

    first->pre = NULL;
    last->next = NULL;
    sq->head->next = sq->tail;
    sq->tail->pre = sq->head;
    sq->hadNum = 0;

    pthread_mutex_unlock(&(sq->mutex));

    if(error!=NULL) *error=1;
    return first;


}

int ReceiverQueueTempInit(Receiver_Queue_Temp** rq){
    if(rq==NULL) return -1;

    *rq=NULL;
    *rq=(Receiver_Queue_Temp*)malloc(sizeof(Receiver_Queue_Temp));
    if(*rq==NULL)  return -1;

    (*rq)->head=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry));
    (*rq)->tail=(Receiver_Queue_Entry*)malloc(sizeof(Receiver_Queue_Entry));
    if((*rq)->head==NULL||(*rq)->tail==NULL){
        free((*rq)->head);
        free((*rq)->tail);
        free(*rq);
        *rq=NULL;
        return -1;
    }

    ReceiverQueueEntryClear((*rq)->head);
    ReceiverQueueEntryClear((*rq)->tail);
    (*rq)->head->next = (*rq)->tail;
    (*rq)->tail->pre = (*rq)->head;

    (*rq)->now = (*rq)->head;

    return 1;
}

int ReceiverQueueTempPush(Receiver_Queue_Temp* rq,Receiver_Queue_Entry* list){

    if(rq == NULL) return 0;

    if(list == NULL) return 1;

    rq->now->next = list;
    list->pre = rq->now;
    Receiver_Queue_Entry* end = list;
    while(end->next!=NULL){
        end = end->next;
    }

    end->next = rq->tail;
    rq->tail->pre = end;

    rq->now = end;

    return 1;
}

Receiver_Queue_Entry ReceiverQueueTempTopAndPop(Receiver_Queue_Temp* rq,int* error){
    
    Receiver_Queue_Entry s;
    ReceiverQueueEntryClear(&s);

    if(rq->head->next==rq->tail) {
        *error = 0;
        return s;
    }

    Receiver_Queue_Entry* e=rq->head->next;

    rq->head->next = e->next;
    e->next->pre = rq->head;

    e->next = NULL;
    e->pre = NULL;

    if(rq->head->next == rq->tail){
        rq->now = rq->head;
    }

    ReceiverQueueEntryCopy(&s,e);

    *error = 1;

    return s;
}
