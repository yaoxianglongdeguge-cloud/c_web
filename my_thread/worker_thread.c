#include "worker_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>


#include "../server/global_resource.h"
#include "../memory_pool/memory_pool.h"
#include "../timer/timer.h"
#include "../http_analysis/http_main.h"
#include "../http_analysis/http_analysis.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../http_analysis/http_back_order.h"
#include "../send_tool/send_tool.h"
#include "../send_tool/send_main.h"
#include "../send_tool/send_tool_early.h"
#include "../send_tool/send_thing_queue.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../connect_config/send_tool_arr_config.h"
#include "../connect_config/connect_manage.h"
#include "../data_struct/Task_queue.h"

extern Task_queue* Task_Queue;

int worker_init(worker** w,int Listen_fd,int id)
{
    *w=(worker*)malloc(sizeof(worker));
    int e0=ed_store_arr_init(*w,10,4,5,8);
    int e1=Memory_pool_init(&((*w)->http_pool),32,4);
    int e2=http_back_order_init(&((*w)->http_order),3);
    int e3=send_tool_arr_init(*w,15,4);
    int e4=Memory_pool_init(&((*w)->send_pool),32,4);
    int e5=Send_thing_queue_init(&((*w)->send_thing_queue),40);
    int e6=send_tool_early_init(&((*w)->send_early),50);
    int e7=timer_init(&((*w)->my_timer),50);

    sem_init(&((*w)->sem_thing_queue_notfull),0,40);
    pthread_mutex_init(&((*w)->mutex_thing), NULL);
    pthread_mutex_init(&((*w)->mutex_pool), NULL);
    my_rwlock_init(&((*w)->rwlock_table));

    (*w)->epfd=epoll_create1(0);
    (*w)->id=id;
    struct epoll_event ev;
    ev.events = EPOLLIN;        
    ev.data.fd = Listen_fd;
    int ret=1;

    ret =epoll_ctl((*w)->epfd, EPOLL_CTL_ADD, Listen_fd, &ev);


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
    if(serial==-1)
    {
        fd_close(w,fd);
        return 1;
    }

    sem_wait(&sem_task_queue_notfull);//本来push里面也没几个操作而且几乎都要直接操作队列，所以放在这里就可以
    pthread_mutex_lock(&mutex_task);

    int e0=Task_queue_push(Task_Queue,w,fd,serial,error_reason,h);

    pthread_mutex_unlock(&mutex_task);
    sem_post(&sem_task_queue_notempty);
}

int receive_and_send_main(worker* w,int Listen_fd,int time)
{
    
    struct epoll_event events[1024];
    while(1)
    {
        int n=-1;
        while(1)
        {
            n=epoll_wait(w->epfd, events, 1024, -1);
            if(n>0)
            {
                break;
            }
            int e4=send_main(w);
            timer_overtime(w->my_timer,time,w);
        }
        for(int i=0;i<n;i++)
        {
            int handle_fd=events[i].data.fd;

            if(handle_fd==Listen_fd)
            {
               int e0=fd_connect(w,handle_fd);
            }
            else
            {
                timer_alloc_and_reset(w->my_timer,handle_fd,w);
                int e1=http_main(handle_fd,w);//错误包已经通过发送程序发了，所以这里的返回值不验证
                int e2=send_main(w);
            }


        }
 
    }
}
