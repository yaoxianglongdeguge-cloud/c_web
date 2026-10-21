#include "../Include/Include.h"

int BuildErrorResponse(char *buf, int buf_size, int error_reason);
char *StatusText(int error_reason);

//一个连接来了，需要三个挂载，epoll，连接计时器，连接管理表
//一个连接可能获得的资源，http请求暂存池，send发送指针池
//一个连接结束需要清理的，epoll读/写，计时器，连接管理表，
//   还有可能分配也可能没有或者已经收回的,请求暂存池，send发送池，并且send池要把剩下的请求发走或者至少通知给业务线程。

int FdConnect(Receiver_Thread* w,int Listen_fd)
{
    int client_fd;
    while(1)
    {
        client_fd=accept(Listen_fd,NULL,NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN) break;
            else  return -1;
        }
        int one=1;                                                   
        setsockopt(client_fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
        
        //挂载epoll
        EpollReadMount(w->epfd,client_fd);

        //挂载连接计时器
        TimerAlloc(w->my_Timer,client_fd,w);

        //挂载连接管理表
        FdTableInsert(w->fd_table,client_fd);

        
    }

    return 1;
}

int FdClose(Receiver_Thread* w,int client_fd,int error_reason)
{
    if(error_reason!=200)
    {
        if(error_reason == 503){ atomic_store(&(w->isFull),1); }
        char re[1024]={0};
        int len=BuildErrorResponse(re,1024,error_reason);
        int n=write(client_fd,re,len);

    }

    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,client_fd,&fd_ob);
    if(fd_ob == NULL) return 1;
    
    //回收send池
    SendPointerPoolDestory(fd_ob->send_pointer_pool,w->store_area);
    //回收http暂存区
    RequestTempPoolDestory(fd_ob->request_temp_pool,w->store_area);

    //连接管理表拆下
    FdTableDelete(w->fd_table,client_fd);
    //计时器
    TimerFree(w->my_Timer,client_fd);
    //epoll
    EpollDelete(w->epfd,client_fd);
    close(client_fd);

    return 1;
}

char* StatusText(int error_reason)
{
    switch (error_reason) {
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

int BuildErrorResponse(char *buf, int buf_size, int error_reason) 
{
    char *reason = StatusText(error_reason);
    int body_len = strlen(reason);

    return snprintf(buf, buf_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        error_reason, reason,
        body_len,
        reason);
}