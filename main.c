#include <stdio.h>
#include <stdlib.h>
#include "my_thread/worker_thread.h"
#include "http_analysis/http_main.h"
#include "memory_pool/memory_pool.h"
#include <fcntl.h>
#include <unistd.h>


int main()
{

   worker* w;
   w=(worker*)malloc(sizeof(worker));
   w->id=0;
   w->epfd=0;

   int a1=Memory_pool_init(&w->http_pool,16,4);
   w->http_ed_store_arr=(http_ed_store**)malloc(sizeof(http_ed_store*));
   int a2=Http_ed_store_init(&w->http_ed_store_arr[0],4096);

   w->my_timer=NULL;
   w->send_pool=NULL;
   w->send_thing_queue=NULL;
   w->send_tool=NULL;
   w->send_tool_table=NULL;
   w->store_pool_table=NULL;

   int fd = open("think.txt", O_RDONLY);



   int a=Http_main(fd,w);



   return 0;
}