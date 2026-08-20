#include "connect_manage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#include "../timer/timer.h"
#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../connect_fd/connect_fd.h"
#include "../send_tool/send_tool.h"
#include "../queue/memory_queue.h"
#include "../data_struct/hash_3.h"


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
        ev.events = EPOLLIN | EPOLLET;   // 边缘触发;
        ev.data.fd = client_fd;          
        epoll_ctl(w->epfd, EPOLL_CTL_ADD, client_fd, &ev);
        
        
        timer_alloc_and_reset(w->my_timer,client_fd,w);
        Fd_Table_insert(w->fd_table,client_fd);

        
    }

    return 1;
}

int fd_close(worker* w,int client_fd)
{
    Fd_Entry* fd_ob=NULL;
    Fd_Table_find(w->fd_table,client_fd,&fd_ob);
    Http_ed_store_destroy(fd_ob->http_store,w->store_area);

    for(int i=0;i<fd_ob->send_tool->blocknum;i++)
    {
        if(fd_ob->send_tool->store[i].use)
        {
            Memory_Queue* m=fd_ob->send_tool->store[i].m_queue;
            int size=fd_ob->send_tool->store[i].size;
            char* c=fd_ob->send_tool->store[i].ptr;
            Memory_Queue_push(m,size,c);
        }
    }


    send_tool_destory(fd_ob->send_tool,w->store_area);
    Fd_Table_delete(w->fd_table,client_fd);

    epoll_ctl(w->epfd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);

    return 1;
}

