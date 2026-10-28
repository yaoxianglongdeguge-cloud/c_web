#include "../../Include/Include.h"


int TimerInit(Timer** t,int num){

    *t=NULL;
    *t=(Timer*)malloc(sizeof(Timer));
    if(*t==NULL) return -1;

    int e0=PriorQueueTimerInit(&((*t)->q),num);
    if(e0!=1) return -1;

    return 1;
}

int TimerExpend(Timer* t)
{
    int cap=2*(t->q->queue[0].fd);
    if(cap<t->q->end) cap=t->q->end;

    Prior_Queue_Timer* q2=NULL;
    if(PriorQueueTimerInit(&q2,cap)!=1) return -1;

    for(int i=0;i<t->q->end;i++)
    {
        q2->queue[i].fd=t->q->queue[i].fd;
        q2->queue[i].onlyId=t->q->queue[i].onlyId;
        q2->queue[i].time=t->q->queue[i].time;
    }
    q2->elem_end=t->q->elem_end;
    q2->queue[0].fd=t->q->queue[0].fd;
    free(t->q);
    t->q=q2;

    return 1;

}


int TimerAlloc(Timer* t,int fd,uint64_t onlyId,Receiver_Thread* w)//分配给连接计时器并重置，如果已经有了那么直接找出并重置
{
    int which=1;
    int f=0;

    while(which>=1&&which<t->q->elem_end){

        if(t->q->queue[which].fd==fd) { f=1; break; }
        which++;
    }

    if(f==1){

        t->q->queue[which].time=time(NULL);
        t->q->queue[which].onlyId=onlyId;
        int e1=PriorQueueTimerDown(t->q,which);
        if(e1!=1) return -1;

    }else if(f==0){

        if(t->q->elem_end==t->q->end){
            if(TimerExpend(t)!=1) return -1;
        }
        time_t now=time(NULL);
        int e1 = PriorQueueTimerInsert(t->q,fd,onlyId,now);
        if(e1!=1) return -1;
        
    }

    return 1;
}

int TimerReset(Timer* t,int fd,uint64_t onlyId,Receiver_Thread* w)
{
    int which=1;

    while(which>=1&&which<t->q->elem_end){

        if(t->q->queue[which].fd==fd) break;
        which++;
    }

    if(which>=t->q->elem_end){

        if(t->q->elem_end==t->q->end){
            if(TimerExpend(t)!=1) return -1;
        }
        if(PriorQueueTimerInsert(t->q,fd,onlyId,time(NULL))!=1) return -1;

        return 1;
    }

    t->q->queue[which].time=time(NULL);
    t->q->queue[which].onlyId=onlyId;
    int e1=PriorQueueTimerDown(t->q,which);
    if(e1!=1) return -1;

    return 1;
}

int TimerOvertime(Timer* t,int overtime,Receiver_Thread* w)//超时时间
{
    if(t->q->elem_end<=1) return 1;

    Timer_Entry t1=PriorQueueTimerTop(t->q);
    time_t now=time(NULL);

    while(t->q->elem_end>1&&now-t1.time>overtime){

        if(PriorQueueTimerPop(t->q)!=1) return -1;

        FdCloseChecked(w,t1.fd,t1.onlyId,408);

        if(t->q->elem_end<=1) break;
        t1=PriorQueueTimerTop(t->q);
    }

    return 1;
}

int TimerFree(Timer* t,int fd)
{
    if(t->q->elem_end==1) return 1;
    
    int i=1;
    int can=0;
    for(;i<t->q->elem_end;i++){

        if(t->q->queue[i].fd==fd) { can=1;  break; }
    }

    if(can!=1) return 1;
    else{
        t->q->queue[i].fd=t->q->queue[t->q->elem_end-1].fd;
        t->q->queue[i].time=t->q->queue[t->q->elem_end-1].time;
        t->q->elem_end--; 
        t->q->queue[0].fd--;
        PriorQueueTimerDown(t->q,i);
    }

    return 1;

}
