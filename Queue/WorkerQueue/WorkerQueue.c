#include "../include.h"



int WorkerQueueInit(Worker_Queue** sq,int blocknum)
{
    *sq=NULL;
    *sq=(Worker_Queue*)malloc(sizeof(Worker_Queue));
    if(*sq==NULL)  return -1;

    (*sq)->queue=(Worker_Queue_Entry*)malloc(sizeof(Worker_Queue_Entry)*blocknum);
    (*sq)->begin=0;
    (*sq)->end=(*sq)->begin+blocknum;
    (*sq)->ptr_in=(*sq)->begin;
    (*sq)->ptr_out=(*sq)->begin;
    (*sq)->num=0;
    (*sq)->blocknum=blocknum;

    sem_init(&((*sq)->queue_notfull),0,blocknum);
    pthread_mutex_init(&((*sq)->mutex),NULL);


    return 1;


}

int WorkerQueuePush(Worker_Queue* sq,int size,char*char_ptr)
{
    sem_wait(&(sq->queue_notfull));
    pthread_mutex_lock(&(sq->mutex));

    sq->queue[sq->ptr_in].size=size;
    sq->queue[sq->ptr_in].char_ptr=char_ptr;

    if(sq->ptr_in+1==sq->end){
      sq->ptr_in=sq->begin;
    }else{
        sq->ptr_in++;
    }

    sq->num++;
    pthread_mutex_unlock(&(sq->mutex));

    return 1;
}

Worker_Queue_Entry WorkerQueueTopAndPop(Worker_Queue* sq,int* error)
{
    pthread_mutex_lock(&(sq->mutex));
    *error=0;
    Worker_Queue_Entry s;
   
    s.size=0;
    s.char_ptr=NULL;
   
    s.size=sq->queue[sq->ptr_out].size;
    s.char_ptr=sq->queue[sq->ptr_out].char_ptr;

     if(sq->num!=0){
      if(sq->ptr_out+1==sq->end){
        sq->ptr_out=sq->begin;
      }else{
        sq->ptr_out++;
      }

      sq->num--;
      *error=1;
    }

    pthread_mutex_unlock(&(sq->mutex));
    sem_post(&(sq->queue_notfull));
    return s;
}






