#include "../include.h"


int deal_task(Request* r,Response* w); //处理业务任务
int pack_task(char** c,Response* w);
int Http_get(profession* profes,char* ptr,Http_analysis_1** h,int size,int* error_reason,worker* w);

int profession_init(profession** profes,int id)
{
  
    *profes=(profession*)malloc(sizeof(profession));
    (*profes)->id=id;
    Memory_Queue_init(&((*profes)->memory_queue),100);
    Memory_Pool_init(&((*profes)->txt_pool),7,7,200,120);
    Memory_Pool_init(&((*profes)->http_pool),6,6,10,10);
    return 1;
}

int deal_and_pack(profession* profes)
{
    int e0=0;
    Task_Entry t;
    
    t=Task_queue_top_and_pop(Task_Queue,&e0);
    
    int Error_reason=t.error_reason;
    int Fd=t.fd;
    int Serial=t.serial;
    worker* W=t.w;
    char* Http=t.http;
    int Size=t.h_size;

    Response Rsp={
        .body=NULL,
        .error_reason=Error_reason,
        .headers=NULL,
        .pool=profes->txt_pool,
        .send_fd=-1,
        .offset=0
    };

    void* ptr=NULL;
    int notfull=0;
    int n=Memory_Pool_alloc(profes->txt_pool,63,&ptr,&notfull);
    if(notfull==0)
    {
        Rsp.error_reason=503;
    }
    Rsp.headers=ptr;
    ptr=ptr+sizeof(Response_Header_Entry);
    Rsp.headers->begin=ptr;
    Rsp.headers->ptr=Rsp.headers->begin;
    Rsp.headers->end=Rsp.headers->begin+64512;


    if(Error_reason==200)
    {
        Http_analysis_1* h;
        Http_get(profes,Http,&h,Size,&Error_reason,W);
        
        if(Error_reason==200)
        {
            Request Req={
                
                
                .Body=(const char* const)h->Body,
                .Headers=(const Hash_map_2* const)h->Headers,
                .Query=(const Hash_map_2* const)h->Query,
                .Method=(const char* const)h->Method,
                .Url=(const char* const)h->Url,
                .Version=(const char* const)h->Version,
  
            };
            
            deal_task(&Req,&Rsp);
            
            Http_analysis_free(h,profes->http_pool);
        }
    }

    off_t file_size=0;
    if(Rsp.send_fd!=-1)
    {
        struct stat st;
        fstat(Rsp.send_fd, &st);
        file_size = st.st_size; 
    }

    off_t totalbody_size=(off_t)Rsp.body_size+file_size;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)totalbody_size);
    Response_header_set(&Rsp,"Content-Length",buf);
   


    //返回请求头和请求体，除了文件
    char* C;
    int allocsize=strlen(Rsp.headers->begin)+Rsp.body_size+17;
    void* ptr2=NULL;
    int notfull2=0;
    int n2=Memory_Pool_alloc(profes->txt_pool,allocsize/1024+1,&ptr2,&notfull2);


    if(notfull2==0)
    {
        Rsp.error_reason=503;
    }
    else
    {
        C=ptr2;
    }

    pack_task(&C,&Rsp);
    if(Rsp.error_reason!=200)
    {
        Send_thing_queue_push(W->Thing_queue, NULL,Fd,Serial,Rsp.error_reason,0,0,0,NULL,-1, 0);
    }
    else
    {

        Send_thing_queue_push(W->Thing_queue, profes->memory_queue,Fd,Serial,200,allocsize,0,file_size,C,Rsp.send_fd,Rsp.offset);
    }
    
    
    
    int e1=1;
    while(e1!=0)
    {
        Memory_Queue_Entry m;
        
        m=Memory_Queue_top_and_pop(profes->memory_queue,&e1);
        if(e1==1)
        {
            Memory_Pool_free(profes->txt_pool,m.char_ptr,(m.size/1024+1)*1024);
        }
    }
        //把打包好的返回文本在内存池里填上，或者如果是错误包也指向对应错误包        
    }
    
    
    
int deal_task(Request* r,Response* w)
{
    char* url=(char*)(r->Url);
    Handler func=Handler_Find(url);

    if(func!=NULL)
    {
        func(r,w);
    }
    else
    {
        Response_error_set(w,404);
    }

    
    

    return 1;

}
    
int pack_task(char** c,Response* w)
{
    if(w->error_reason!=200)
    {
        if(w->body!=NULL)
        {
            Memory_Pool_free((Memory_Pool*)(w->pool),w->body,(w->body_size/1024+1)*1024);
        }

        if(w->headers!=NULL)
        {
            Memory_Pool_free((Memory_Pool*)(w->pool),w->headers,64512);
        }

        if(w->send_fd!=-1)
        {
            close(w->send_fd);
        }

        return 1;

    }
    else
    {
        char* a="HTTP/1.1 200 OK\r\n";
        strcpy(*c,a);
        if(w->headers->begin<w->headers->ptr)
        {
            strcat(*c,w->headers->begin);
            Memory_Pool_free((Memory_Pool*)(w->pool),w->headers,64512);
        }
        if(w->body!=NULL)
        {
            strcat(*c,w->body);
            Memory_Pool_free((Memory_Pool*)(w->pool),w->body,(w->body_size/1024+1)*1024);
        }
        

    }
        
    return 1;
}
    
int Http_get(profession* profes,char* ptr,Http_analysis_1** h,int size,int* error_reason,worker* w)
{
    int allocsize=4*(size)+sizeof(Http_analysis_1)+2*sizeof(Hash_map_2)+sizeof(Hash2_Entry_2)*14;
  
    Http_analysis_init(h,profes->http_pool,allocsize);
        
    memcpy((*h)->ptr,ptr,size);
    char* ptr2=(*h)->ptr;
    Memory_Pool_free(w->http_pool,ptr,1024*(size/1024+1));
    (*h)->ptr=(*h)->ptr+size;
    Http_analysis_receive(*h,ptr2,error_reason);
    if(*error_reason!=200)
    {
        Http_analysis_free((*h),profes->http_pool);
    }
        
    return 1;
}

    
    
    
    