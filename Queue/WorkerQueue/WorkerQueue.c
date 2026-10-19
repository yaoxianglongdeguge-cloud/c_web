#include "../../Include/Include.h"



int WorkerQueueInit(Worker_Queue** sq)
{
    if(sq==NULL) return -1;

    *sq=NULL;
    *sq=(Worker_Queue*)malloc(sizeof(Worker_Queue));
    if(*sq==NULL)  return -1;

    (*sq)->head=(Worker_Queue_Entry*)malloc(sizeof(Worker_Queue_Entry));
    (*sq)->tail=(Worker_Queue_Entry*)malloc(sizeof(Worker_Queue_Entry));
    if((*sq)->head==NULL||(*sq)->tail==NULL){
        free((*sq)->head);
        free((*sq)->tail);
        free(*sq);
        *sq=NULL;
        return -1;
    }

    (*sq)->head->NeedFree=NULL;
    (*sq)->head->next = (*sq)->tail;
    (*sq)->head->pre = NULL;
    (*sq)->tail->NeedFree=NULL;
    (*sq)->tail->next = NULL;
    (*sq)->tail->pre = (*sq)->head;
    (*sq)->hadNum = 0;

    pthread_mutex_init(&((*sq)->mutex),NULL);

    return 1;
}

int WorkerQueuePush(Worker_Queue* sq,int size,char*char_ptr)
{
    if(sq==NULL||char_ptr==NULL||size<0) return 0;

    //节点位置由“响应实际字节数”唯一决定，调用方必须和 PackTotal 用同一个 size
    Worker_Queue_Entry* e = (Worker_Queue_Entry*)(char_ptr + WORKER_QUEUE_NODE_OFFSET(size));
    e->NeedFree = char_ptr;                 //释放时要用块基址，不是节点地址

    pthread_mutex_lock(&(sq->mutex));
    e->next = sq->tail;
    e->pre = sq->tail->pre;
    sq->tail->pre->next = e;
    sq->tail->pre = e;
    sq->hadNum++;
    pthread_mutex_unlock(&(sq->mutex));

    return 1;
}

//一次性摘下整条队列，返回单链表头；空队列返回 NULL。
//摘链和 hadNum 归零在同一把锁内完成，避免和 WorkerQueuePush 的尾部追加交错。
Worker_Queue_Entry* WorkerQueueTopAndPop(Worker_Queue* sq,int* error)
{
    if(error!=NULL) *error=0;
    if(sq==NULL) return NULL;

    pthread_mutex_lock(&(sq->mutex));

    Worker_Queue_Entry* first = sq->head->next;
    if(first == sq->tail){
        pthread_mutex_unlock(&(sq->mutex));
        return NULL;
    }

    Worker_Queue_Entry* last = sq->tail->pre;

    first->pre = NULL;
    last->next = NULL;
    sq->head->next = sq->tail;
    sq->tail->pre = sq->head;
    sq->hadNum = 0;

    pthread_mutex_unlock(&(sq->mutex));

    if(error!=NULL) *error=1;
    return first;
}
