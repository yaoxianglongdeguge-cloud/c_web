#include "send_tool_arr_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h> 
#include <pthread.h> 


#include "../my_thread/worker_thread.h"
#include "../http_analysis/http_ed_store.h"
#include "../http_analysis/http_state.h"
#include "../send_tool/send_tool.h"

typedef struct Send_table_Entry{

    int fd;
    sem_t sem;
    pthread_mutex_t mutex;

}Send_table_Entry;