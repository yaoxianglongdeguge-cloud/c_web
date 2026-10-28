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
            if (errno == EAGAIN||errno == EWOULDBLOCK) break;

            //fd 用尽是 accept 失败最常见的原因，限流打印避免刷屏
            static int accept_warned=0;
            if(accept_warned<3){
                accept_warned++;
                if(errno==EMFILE||errno==ENFILE)
                    fprintf(stderr,"[Net] accept 失败：fd 用尽（errno=%d，%s）；检查 ulimit -n / cgroup 限制\n",
                            errno,strerror(errno));
                else
                    fprintf(stderr,"[Net] accept 失败：errno=%d (%s)\n",errno,strerror(errno));
            }
            return -1;
        }
        int one=1;                                                   
        if(setsockopt(client_fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))!=0){
            static int nodelay_warned=0;
            if(nodelay_warned<3){
                nodelay_warned++;
                fprintf(stderr,"[Net] setsockopt(TCP_NODELAY) 失败：errno=%d (%s)\n",errno,strerror(errno));
            }
        }
        
        //挂载epoll
        EpollReadMount(w->epfd,client_fd);

        //挂载连接管理表
        FdTableInsert(w->fd_table,client_fd);

        //挂载连接计时器
        Fd_Entry* fd_ob=NULL;
        if(FdTableFind(w->fd_table,client_fd,&fd_ob)==1&&fd_ob!=NULL){
            TimerAlloc(w->my_Timer,client_fd,fd_ob->onlyId,w);
        }

        
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

//这个是给那些没有实现判断连接是否是同一代就关闭的操作特供的
//比如计时器删除节点就不会先判断有没有这个连接是不是同一代
int FdCloseChecked(Receiver_Thread* w,int client_fd,uint64_t onlyId,int error_reason)
{
    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,client_fd,&fd_ob);

    if(fd_ob==NULL||fd_ob->onlyId!=onlyId) return 1;

    return FdClose(w,client_fd,error_reason);
}

enum{
    HTTP_STATUS_BAD_REQUEST             = 400,
    HTTP_STATUS_NOT_FOUND               = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED      = 405,
    HTTP_STATUS_REQUEST_TIMEOUT         = 408,
    HTTP_STATUS_LENGTH_REQUIRED         = 411,
    HTTP_STATUS_PAYLOAD_TOO_LARGE       = 413,
    HTTP_STATUS_URI_TOO_LONG            = 414,
    HTTP_STATUS_HEADERS_TOO_LARGE       = 431,
    HTTP_STATUS_INTERNAL_ERROR          = 500,
    HTTP_STATUS_UNAVAILABLE             = 503
};

char* StatusText(int error_reason)
{
    switch (error_reason) {
        case HTTP_STATUS_BAD_REQUEST:        return "Bad Request";
        case HTTP_STATUS_NOT_FOUND:          return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_STATUS_REQUEST_TIMEOUT:    return "Request Timeout";
        case HTTP_STATUS_LENGTH_REQUIRED:    return "Length Required";
        case HTTP_STATUS_PAYLOAD_TOO_LARGE:  return "Payload Too Large";
        case HTTP_STATUS_URI_TOO_LONG:       return "URI Too Long";
        case HTTP_STATUS_HEADERS_TOO_LARGE:  return "Header Fields Too Large";
        case HTTP_STATUS_INTERNAL_ERROR:     return "Internal Server Error";
        case HTTP_STATUS_UNAVAILABLE:        return "Service Unavailable";
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