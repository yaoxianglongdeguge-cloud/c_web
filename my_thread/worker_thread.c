#include "worker_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "../server/global_resource.c"
#include "../memory_pool/memory_pool.h"
#include "../timer/timer.h"
#include "../http_analysis/http_analysis.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../http_analysis/http_back_order.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../send_tool/send_tool.h"
#include "../send_tool/send_thing_queue.h"
#include "send_tool_arr_config.h"

extern Task_queue* Task_Queue;

int worker_init(worker** w)
{
    int e0=ed_store_arr_init(*w,10,4,5,8);
    int e1=Memory_pool_init(&((*w)->http_pool),32,4);
    int e2=http_back_order_init(&((*w)->http_order),3);
    int e3=send_tool_arr_init(*w,15,2);
    int e4=Memory_pool_init(&((*w)->send_pool),32,4);
    int e5=Send_thing_queue_init((*w)->send_thing_queue,40);

    sem(&((*w)->sem_thing_queue_notfull),0,40);
    pthread_mutex_t mutex_thing = PTHREAD_MUTEX_INITIALIZER;



    if(e0!=1)
    {
        return -1;
    }
    if(e1!=1)
    {
        return -1;
    }
    if(e2!=1)
    {
        return -1;
    }
    if(e3!=1)
    {
        return -1;
    }
    if(e4!=1)
    {
        return -1;
    }
    if(e5!=1)
    {
        return -1;
    }

    return 1;

}

int worker_to_profession(worker* w,int fd,Http_analysis_1* h,int error_reason,int serial)
{
    sem_wait(&sem_task_queue_notempty);//本来push里面也没几个操作而且几乎都要直接操作队列，所以放在这里就可以
    pthread_mutex_lock(&mutex_task);

    int e0=Task_queue_push(Task_Queue,w,fd,serial,error_reason,h);

    pthread_mutex_unlock(&mutex_task);
    sem_post(&sem_task_queue_notfull);
}

int receive_and_send_main(worker* w,int Listen_fd)
{
    struct epoll_event events[1024];
    struct epoll_event ev;

    while(1)
    {
        int n=epoll_wait(w->epfd, events, 1024, -1);
        for(int i=0;i<n;i++)
        {
            int handle_fd=events[i].data.fd;

            if(handle_fd==Listen_fd)
            {
                int client_fd=accept(handle_fd,NULL,NULL);
                int flags = fcntl(client_fd, F_GETFL, 0);   // 从内核拿到当前标志
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK); // 加上非阻塞，写回内核
                ev.events = EPOLLIN;        
                ev.data.fd = client_fd;          
                epoll_ctl(w->epfd, EPOLL_CTL_ADD, client_fd, &ev);  
                int e0=fd_connect(w,client_fd);
                if(e0!=1)
                {
                    epoll_ctl(w->epfd, EPOLL_CTL_DEL, client_fd, NULL);
                }
        
            }
            else
            {
                int e1=http_main(w,handle_fd);//错误包已经通过发送程序发了，所以这里的返回值不验证
                int e2=send_main(w);
   

            }

        }
 
    }
}
