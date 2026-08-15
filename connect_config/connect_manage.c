#include "connect_manage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../timer/timer.h"
#include "../my_lock/my_rwlock_t.h"
#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../connect_config/send_tool_arr_config.h"
#include "../http_analysis/http_back_order.h"
#include "../send_tool/send_tool_early.h"


int fd_connect(worker* w,int Listen_fd)
{
    struct epoll_event ev;
    int client_fd=accept(handle_fd,NULL,NULL);
    int flags = fcntl(client_fd, F_GETFL, 0);   // 从内核拿到当前标志
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK); // 加上非阻塞，写回内核
    ev.events = EPOLLIN;        
    ev.data.fd = client_fd;          
    epoll_ctl(w->epfd, EPOLL_CTL_ADD, client_fd, &ev);  
    int e0=timer_alloc_and_reset(w->my_timer,handle_fd,w);
    if(e0!=1)
    {   
        epoll_ctl(w->epfd, EPOLL_CTL_DEL, client_fd, NULL);
        return 0;
    }
    
    return 1;
}

int fd_close(worker* w,int client_fd)
{
    int e0=ed_store_pool_fdfree(w.client_fd);

    my_lock_wrlock(&(w->rwlock_table));
    int e2=send_tool_arr_fdfree(w,client_fd);
    my_lock_unlock(&(w->rwlock_table));

    int e1=http_back_order_deletefd(w->http_order,client_fd);
    int e3=send_tool_early_pop(w->send_early,client_fd);

    return 1;
}

