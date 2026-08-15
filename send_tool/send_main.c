#include "send_main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../my_thread/worker_thread.h"
#include "send_thing_queue.h"

int send_main(worker* w)
{
    int e0=0;
    Send_tq_Entry s;
    pthread_mutex_lock(&(w->mutex_thing));
    s = Send_thing_queue_top_and_pop(&(w->send_thing_queue),&e0);

    if(e0!=1)
    {
        
    }

    sem_post(&(w->sem_thing_queue_notfull));
    pthread_mutex_unlock(&(w->mutex_thing));

}

