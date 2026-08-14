#include "receive_and_send.h"
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>



#include "../my_thread/worker_thread.h"
#include "http_analysis.h"
#include "http_ed_store.h"
#include "http_state.h"
#include "http_back_order.h"
#include "../connect_config/ed_store_arr_config.h"
#include "../connect_config/connect_manage.h"
#include "../timer/timer.h"
#include "../server/receive_and_send.h"


int receive_and_send_main(worker* w)
{
    struct epoll_event events[1024];

    while(1)
    {
        epoll_wait
    }
}


