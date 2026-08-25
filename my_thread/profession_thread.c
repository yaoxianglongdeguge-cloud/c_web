#include "../include.h"

int deal_task(int * e); //处理业务任务
int pack_task(char** c,Response);
int Http_get(profession* profes,char* ptr,Http_analysis_1** h,int size,int* error_reason,worker* w);

int profession_init(profession** profes,int id)
{
    *profes=(profession*)malloc(sizeof(profession));
    (*profes)->id=id;
    Memory_Queue_init(&((*profes)->memory_queue),100);
    Memory_Pool_init(&((*profes)->txt_pool),6,6,500,120);
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
    Response Rsp;
    
    if(Error_reason==200)
    {
        Http_analysis_1* h;
        Http_get(profes,Http,&h,Size,&Error_reason,W);
        
        if(Error_reason==200)
        {
            Request H;

            //H.Body=(const char* const)h->Body;
            //H.Headers=(const Hash_map_2* const)h->Headers;
            //H.Query=(const Hash_map_2* const)h->Query;
           // H.Method=(const char* const)h->Method;
           // H.Url=(const char* const)h->Url;
           // H.Version=(const char* const)h->Version;
            
            deal_task(&Error_reason);

            Http_analysis_free(h,profes->http_pool);
        }
    }
    
    int size=0;
    char* C;
    size=pack_task(&C,Rsp);

    //int Send_thing_queue_push(Send_thing_queue* sq,Memory_Queue* m_queue,int fd,int
    // serial,int error_reason,int resp_size,int file_size,char*char_ptr,int send_fd,off_t offset)

    int file_fd = open("test10mb.jpeg", O_RDONLY);
    struct stat st;
    stat("test10mb.jpeg", &st);
    off_t size_file = st.st_size;
    Send_thing_queue_push(W->Thing_queue,profes->memory_queue,Fd,Serial,Error_reason,size,size_file,C,file_fd,0);
    
    
    
    int e1=1;
    while(e1!=0)
    {
        Memory_Queue_Entry m;
        
        m=Memory_Queue_top_and_pop(profes->memory_queue,&e1);
        /*if(e1==1)
        {
            Memory_Pool_free(profes->txt_pool,m.char_ptr,m.size);
            }*/
        }
        
        
        //把打包好的返回文本在内存池里填上，或者如果是错误包也指向对应错误包        
    }
    
    
    
int deal_task(int * e)
{
    *e=200;
    return 1;
}
    
int pack_task(char** c,Response)
{
    *c="HTTP/1.1 200 OK\r\n"
    "Content-Type: image/png\r\n"
    "Content-Length: 76310\r\n"
    "\r\n";
        
    int len = strlen(*c);
        
    return len;
}
    
int Http_get(profession* profes,char* ptr,Http_analysis_1** h,int size,int* error_reason,worker* w)
{
    Http_analysis_init(h,profes->http_pool,4*size);
        
    memcpy((*h)->ptr,ptr,size);
     Memory_Pool_free(w->http_pool,ptr,size);
    (*h)->ptr=(*h)->ptr+size;
    Http_analysis_receive(*h,ptr,error_reason);
    if(*error_reason!=200)
    {
        Http_analysis_free((*h),profes->http_pool);
    }
        
    return 1;
}

    
    
    
    