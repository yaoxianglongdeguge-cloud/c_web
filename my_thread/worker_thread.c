#include "../include.h"

int worker_init(worker** w,int Listen_fd,int id)
{

    *w=(worker*)malloc(sizeof(worker));

    Memory_Pool_init(&((*w)->http_pool),6,6,500,400);
    Memory_Pool_init(&((*w)->store_area),10,6,500,400);
    Send_thing_queue_init(&((*w)->Thing_queue),100);
    timer_init(&((*w)->my_timer),5000);
    Fd_Table_init(&((*w)->fd_table),4);




    (*w)->epfd=epoll_create1(0);
    (*w)->id=id;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = Listen_fd;
    epoll_ctl((*w)->epfd, EPOLL_CTL_ADD, Listen_fd, &ev);

    return 1;

}

int worker_to_profession(worker* w,int fd,char* h,int h_size,int error_reason,int serial)
{
    int e0=Task_queue_push(Task_Queue,w,fd,serial,error_reason,h,h_size);
}

int receive_and_send_main(worker* w,int Listen_fd,int time,int ed_store_blocknum)
{
    
    struct epoll_event events[1024];
    while(1)
    {
        int n=-1;
        while(1)
        {
            n=epoll_wait(w->epfd,events,1024,50);
            int e2=send_main(w,ed_store_blocknum);
            if(n>0)
            {
                break;
            }
        }
        timer_overtime(w->my_timer,time,w);
        for(int i=0;i<n;i++)
        {
            int handle_fd=events[i].data.fd;
            if(handle_fd<3)
            {
                continue;
            }

            if(handle_fd==Listen_fd)
            {
               int e0=fd_connect(w,handle_fd);
            }
            else
            {
                timer_alloc_and_reset(w->my_timer,handle_fd,w);
                int e1=http_main(handle_fd,w,ed_store_blocknum);//错误包已经通过发送程序发了，所以这里的返回值不验证
                int e2=send_main(w,ed_store_blocknum);
            }


        }
 
    }
    
    return 1;
}
