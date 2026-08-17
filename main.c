#include <stdio.h>
#include <stdlib.h>
#include "my_thread/worker_thread.h"
#include "my_thread/profession_thread.h"
#include "http_analysis/http_main.h"
#include "memory_pool/memory_pool.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "server/global_resource.h"
#include <time.h>

typedef  struct main{
    worker* w;
    int fd;
    int time;

} main_t;

void* func1(void* arg)
{
    main_t* m=(main_t*)arg;
    receive_and_send_main(m->w,m->fd,m->time);
}
void* func2(void* arg)
{
    while(1)
    {
    deal_and_pack();
    }
}

int main() {

    global_resource_init(100);
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 1);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    worker* w1;
    worker_init(&w1,fd,1);

    pthread_t tid1;
    pthread_t tid2;

    main_t m;
    m.fd=fd;
    m.time=10000;
    m.w=w1;

    

    //pthread_create(&tid1, NULL,func1 ,&m);

    pthread_create(&tid2, NULL,func2, NULL);

 
    receive_and_send_main(m.w,m.fd,m.time);


    return 0;
}