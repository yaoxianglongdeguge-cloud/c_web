#include "../include.h"

int build_error_response(char *buf, int buf_size, int error_reason);
char *status_text(int error_reason);


int fd_connect(worker* w,int Listen_fd)
{
    struct epoll_event ev;
    int client_fd;
    while(1)
    {
        client_fd=accept(Listen_fd,NULL,NULL);
        if (client_fd < 0) {
        if (errno == EAGAIN) break;
        else 
        {
            return -1;
        }
       }
        int flags = fcntl(client_fd, F_GETFL, 0); 
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK); 
        ev.events = EPOLLIN; 
        ev.data.fd = client_fd;          
        epoll_ctl(w->epfd, EPOLL_CTL_ADD, client_fd, &ev);
        
        
        timer_alloc_and_reset(w->my_timer,client_fd,w);
        Fd_Table_insert(w->fd_table,client_fd);

        
    }

    return 1;
}

int fd_close(worker* w,int client_fd,int error_reason)
{
    if(error_reason!=200)
    {
        char re[1024]={0};
        int len=build_error_response(re,1024,error_reason);
        int n=write(client_fd,re,len);

    }
    Fd_Entry* fd_ob=NULL;
    Fd_Table_find(w->fd_table,client_fd,&fd_ob);
    
    Http_ed_store_destroy(fd_ob->http_store,w->store_area);
    for(int i=0;i<fd_ob->send_tool->blocknum;i++)
    {
        if(fd_ob->send_tool->store[i].use==1)
        {
            Memory_Queue* m=fd_ob->send_tool->store[i].m_queue;
            int size=fd_ob->send_tool->store[i].size_resp;
            char* c=fd_ob->send_tool->store[i].ptr;

            if(m!=NULL)
            {
                Memory_Queue_push(m,size,c);
            }
            if(fd_ob->send_tool->store[i].send_fd!=-1)
            {
                close(fd_ob->send_tool->store[i].send_fd);
            }
        }
    }


    send_tool_destory(fd_ob->send_tool,w->store_area);
    Fd_Table_delete(w->fd_table,client_fd);
    timer_free(w->my_timer,client_fd);

    epoll_ctl(w->epfd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);

    return 1;
}

char *status_text(int error_reason)
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

int build_error_response(char *buf, int buf_size, int error_reason) 
{
    char *reason = status_text(error_reason);
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