#include "../../../include.h"




int PriorQueueTimerInit(Prior_Queue_Timer** p,int num)//节点个数
{
    (*p)=(Prior_Queue_Timer*)malloc(sizeof(Prior_Queue_Timer));
    if((*p)==NULL) return -1;
    
    (*p)->queue=NULL;
    (*p)->queue=(Timer_Entry*)malloc((num+1)*sizeof(Timer_Entry));
    if((*p)->queue==NULL) return -1;
    
    (*p)->end=num+1;
    (*p)->elem_end=1;

    for(int i=0;i<(*p)->end;i++){
        (*p)->queue[i].fd=-2;
        (*p)->queue[i].time=-2;
    }

    (*p)->queue[0].fd=0;

    return 1;
    
}


int PriorQueueTimerSwap(Prior_Queue_Timer* p,int t1,int t2)
{
    time_t time=p->queue[t2].time;
    int fd=p->queue[t2].fd;
    
    p->queue[t2].time=p->queue[t1].time;
    p->queue[t2].fd=p->queue[t1].fd;
    
    p->queue[t1].time=time;
    p->queue[t1].fd=fd;
    
    return 1;
}

int PriorQueueTimerUp(Prior_Queue_Timer* p,int t)//上滤
{
    if(t<1||t>=p->elem_end||t>=p->end) return 0;
    
    
    while(t>1){
        if(p->queue[t].time<p->queue[t/2].time){

            int a1=PriorQueueTimerSwap(p,t,t/2);
            if(a1!=1) return -1;
            
            t=t/2;
        }else{ break; }
    }
    
    return 1;
    
}

int PriorQueueTimerDown(Prior_Queue_Timer* p,int t)
{
    if(t<1||t>=p->elem_end||t>=p->end) return 0;
    
    while(t*2<p->elem_end){

        if(t*2+1>=p->elem_end){

            if(p->queue[t].time>p->queue[t*2].time){
                int a1=PriorQueueTimerSwap(p,t,t*2);
                if(a1!=1) return -1;
                    
                t=t*2;      
            }else{ break; }
        }else{

            if(p->queue[t*2].time<=p->queue[t*2+1].time){
                if(p->queue[t].time>p->queue[t*2].time){
                    int a1=PriorQueueTimerSwap(p,t,t*2);
                    if(a1!=1) return -1;
                    
                    t=t*2;  
                }else{ break; }
                
            }else{
                if(p->queue[t].time>p->queue[t*2+1].time){
                    int a1=PriorQueueTimerSwap(p,t,t*2+1);
                    if(a1!=1) return -1;
                    
                    t=t*2+1;
                    
                }else{ break;}
                
            }
            
        }
        
    }

    return 1;
}

int PriorQueueTimerInsert(Prior_Queue_Timer* p,int fd,time_t time)
{
    if(p->elem_end>=p->end) return 0;

    p->queue[p->elem_end].fd=fd;
    p->queue[p->elem_end].time=time;
    p->elem_end=p->elem_end+1;
    
    int a=PriorQueueTimerUp(p,p->elem_end-1);

    if(a!=1) return -1;

    p->queue[0].fd++;

    return 1;

}

int PriorQueueTimerPop(Prior_Queue_Timer* p)
{
    if(p->elem_end==2)
    {
        p->elem_end=1;
        p->queue[0].fd--;
        return 1;
    }

    p->queue[1].fd=p->queue[p->elem_end-1].fd;
    p->queue[1].time=p->queue[p->elem_end-1].time;
    p->elem_end=p->elem_end-1;

    int a=PriorQueueTimerDown(p,1);
    
    if(a!=1) return -1;

    p->queue[0].fd--;

    return 1;


}

Timer_Entry PriorQueueTimerTop(Prior_Queue_Timer* p){
    
    Timer_Entry t;

    if(p->queue[0].fd>0)
    {
        t.fd=p->queue[1].fd;
        t.time=p->queue[1].time;
    }else{
        t.fd=-2;
        t.time=-2;
    }

    return t;
}
