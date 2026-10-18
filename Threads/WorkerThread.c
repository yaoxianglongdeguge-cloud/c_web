#include "../Include/Include.h"


int DealWithTask(Request* r,Response* w); //处理业务任务
int PackBackHead(char** c,Response* w);
int RequestGet(Worker_Thread* profes,char* ptr,Request_Analysiser** h,int size,int* error_reason,Receiver_Thread* w);

int RspHeaderAlloc(Worker_Thread* profes,Response* Rsp){

    void* ptr=NULL;
    int notfull=0;
    MemoryPoolAlloc(profes->txt_pool,63*1024,&ptr,&notfull);
    if(notfull==0){
        /* 【保险】池满时不要继续用 NULL 指针写 headers->begin（原来会直接段错误崩溃） */
        Rsp->error_reason=503;
        Rsp->headers=NULL;
        return 0;
    }
    
    Rsp->headers=ptr;
    ptr=ptr+sizeof(Response_Header_Entry);
    Rsp->headers->begin=ptr;
    Rsp->headers->ptr=Rsp->headers->begin;
    Rsp->headers->end=Rsp->headers->begin+64512;

    return 1;

}

int PackTotal(Worker_Thread* profes,Response* Rsp,char** C,int* allocsize,off_t* file_size){

    //如果有要发的文件，计算文件大小
    *file_size=0;
    if(Rsp->send_fd!=-1){
        struct stat st;
        fstat(Rsp->send_fd, &st);
        *file_size = st.st_size; 
    }

    //计算文件和请求体总的大小，并计入响应头
    off_t totalbody_size=(off_t)Rsp->body_size+*file_size;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)totalbody_size);
    ResponseHeaderSet(Rsp,"Content-Length",buf);
   


    //返回请求头和请求体，文件不用打包，直接把fd传给接待线程
    *allocsize=strlen(Rsp->headers->begin)+Rsp->body_size+17;
    void* ptr2=NULL;
    int notfull2=0;
    MemoryPoolAlloc(profes->txt_pool,1024*(*allocsize/1024+1),&ptr2,&notfull2);

    if(notfull2==0)  Rsp->error_reason=503;
    else  *C = ptr2;

    PackBackHead(C,Rsp);

    return 1;

}

int CleanMemoryQueue(Worker_Thread* profes){

    int e1=1;
    while(e1!=0){
        Worker_Queue_Entry m=WorkerQueueTopAndPop(profes->memory_queue,&e1);
        if(e1==1){
            MemoryPoolFree(profes->txt_pool,m.char_ptr);
        }
    }
    return 1;
    
}

int WorkerThreadInit(Worker_Thread** profes,int id)
{
  
    *profes=(Worker_Thread*)malloc(sizeof(Worker_Thread));
    (*profes)->id=id;
    WorkerQueueInit(&((*profes)->memory_queue),100);
    MemoryPoolInit(&((*profes)->txt_pool),2048*1024,8);
    MemoryPoolInit(&((*profes)->http_pool),2048*1024,8);
    return 1;
}

int sleepInit(struct timespec* ts){
    ts->tv_nsec = 0;
    ts->tv_sec = 0;

    return 1;
}

int sleepAdd(struct timespec* ts){
    if(ts->tv_nsec == 1000000){
        return 1;
    }else{
        ts->tv_nsec = ts->tv_nsec+100000;
    }

    return 1;
}

int GetFromTaskQueue(Task_Queue_Entry* t,Worker_Thread* profes){

    while(1){

        int e0=0;
        *t=TasKQueueTopAndPop(taskQueue,&e0);
        if(e0!=1){ 
           CleanMemoryQueue(profes);
           struct timespec wait_ts;
           clock_gettime(CLOCK_REALTIME,&wait_ts);
           wait_ts.tv_nsec+=100000000;
           if(wait_ts.tv_nsec>=1000000000){ wait_ts.tv_nsec-=1000000000; wait_ts.tv_sec++; }
           sem_timedwait(&(taskQueue->sem_task_notempty),&wait_ts);
        }else{
            break;
        }
    }

    return 1;

}


int WorkerMain(Worker_Thread* profes){

    Task_Queue_Entry t;
    GetFromTaskQueue(&t,profes);
    
    //拿到请求
    int Error_reason=t.error_reason;
    int Fd=t.fd;
    int Serial=t.serial;
    Receiver_Thread* W=t.w;
    char* Http=t.http;
    int Size=t.h_size;

    //在栈上分配返回对象打包对象
    Response Rsp={
        .body=NULL,
        .error_reason=Error_reason,
        .headers=NULL,
        .pool=profes->txt_pool,
        .send_fd=-1,
        .offset=0
    };
    //打包对象头部行分配
    RspHeaderAlloc(profes,&Rsp);

    //任务执行
    if(Error_reason==200){

        Request_Analysiser* h;
        RequestGet(profes,Http,&h,Size,&Error_reason,W);
        
        if(Error_reason==200){
            Request Req={
                
                .Body=(const char* const)h->Body,
                .Headers=(const Analysiser_Hash* const)h->Headers,
                .Query=(const Analysiser_Hash* const)h->Query,
                .Method=(const char* const)h->Method,
                .Url=(const char* const)h->Url,
                .Version=(const char* const)h->Version,
  
            };
            
            DealWithTask(&Req,&Rsp);
            
            RequestAnalysisFree(h,profes->http_pool);
        }else{
            Rsp.error_reason=Error_reason;
        }
    }
    if(Rsp.headers==NULL){ Rsp.error_reason=503; }

    //返回请求头和请求体，除了文件
    //把信息都打包到返回对象里然后传给接待线程
    
    if(Rsp.error_reason!=200){

        { char* C=NULL; PackBackHead(&C,&Rsp); }

        ReceiverQueuePush(W->Thing_queue, NULL,Fd,Serial,Rsp.error_reason,0,0,0,NULL,-1, 0,(void*)Http);
        ReceiverNotify(W);   
    }else{

        char* C;
        int allocsize=0;
        off_t file_size=0;
        PackTotal(profes,&Rsp,&C,&allocsize,&file_size);
        ReceiverQueuePush(W->Thing_queue, profes->memory_queue,Fd,Serial,200,allocsize,0,
            file_size,C,Rsp.send_fd,Rsp.offset,(void*)Http);
        ReceiverNotify(W);   

    }
    
    //最后清理一下已经发完的包
    CleanMemoryQueue(profes);
        
    }
    
    
    
int DealWithTask(Request* r,Response* w)
{
    char* url=(char*)(r->Url);
    Handler func=RouterHandlerFind(url);

    if(func!=NULL)  func(r,w);
    else  ResponseErrorSet(w,404);

    return 1;

}
    
int PackBackHead(char** c,Response* w)
{
    if(w->error_reason!=200)
    {
        if(w->body!=NULL){
            MemoryPoolFree((Memory_Pool*)(w->pool),w->body);
        }

        if(w->headers!=NULL){
            MemoryPoolFree((Memory_Pool*)(w->pool),w->headers);
        }

        if(w->send_fd!=-1){
            close(w->send_fd);
        }

        return 1;

    }else{

        char* a="HTTP/1.1 200 OK\r\n";
        strcpy(*c,a);
        if(w->headers->begin<w->headers->ptr){
            strcat(*c,w->headers->begin);
            MemoryPoolFree((Memory_Pool*)(w->pool),w->headers);
        }
        if(w->body!=NULL){
            strcat(*c,w->body);
            MemoryPoolFree((Memory_Pool*)(w->pool),w->body);
        }
        
    }
        
    return 1;
}
    
int RequestGet(Worker_Thread* profes,char* ptr,Request_Analysiser** h,int size,int* error_reason,Receiver_Thread* w)
{
    //分析块 = 请求原文 + 结构体 + 两块 hash（结构/桶/溢出条目）
    //原来写 4*size：1MB 的 body 会需要 4MB，超过池子 perSize 直接失败（memcpy(NULL) 崩溃）
    int allocsize=size+sizeof(Request_Analysiser)+AnalysiserHashMemOverhead();

    if(RequestAnalysisInit(h,profes->http_pool,allocsize)!=1||(*h)==NULL){
        *error_reason=503;
        return 0;
    }

    memcpy((*h)->ptr,ptr,size);
    char* ptr2=(*h)->ptr;
    (*h)->ptr=(*h)->ptr+size;
    RequestAnalysisReceive(*h,ptr2,error_reason);
    if(*error_reason!=200){
        RequestAnalysisFree((*h),profes->http_pool);
    }
        
    return 1;
}

    
    
    
    