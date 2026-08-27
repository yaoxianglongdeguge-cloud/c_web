#include "include.h"

typedef  struct main{
    worker* w;
    int fd;
    int time;
    int ed_store_blocknum;

} main_t;

typedef  struct main2{
    profession* p;
} main_t2;

void* func1(void* arg)
{
    main_t* m=(main_t*)arg;
    receive_and_send_main(m->w,m->fd,m->time,m->ed_store_blocknum);
}
void* func2(void* arg)
{
    main_t2* m=(main_t2*)arg;
    while(1)
    {
    deal_and_pack(m->p);
    }
}


typedef void (*HandlerFunc)();

int Web_Driver(char* IPaddr,int PORT, HandlerFunc Handler) 
{

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    global_resource_init(100);

    Handler();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(IPaddr);  
    addr.sin_port = htons(PORT);

    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 100);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);





    pthread_t tid2;
    
    profession* p1;
    profession_init(&p1,1);
    main_t2 m5 = {.p=p1};
    
    pthread_create(&tid2, NULL, func2, &m5);

    /*
// 创建worker
pthread_t tid1;

worker* w2;
worker_init(&w2, fd, 2);
worker* w3;
worker_init(&w3, fd, 3);
worker* w4;
worker_init(&w4, fd, 4);

main_t m2 = {.fd = fd, .time = 10000, .w = w2,.ed_store_blocknum=100};
main_t m3 = {.fd = fd, .time = 10000, .w = w3,.ed_store_blocknum=100};
main_t m4 = {.fd = fd, .time = 10000, .w = w4,.ed_store_blocknum=100};


pthread_create(&tid1, NULL, func1, &m2);

pthread_create(&tid1, NULL, func1, &m3);

pthread_create(&tid1, NULL, func1, &m4);
*/

profession* p2;
profession_init(&p2,2);

main_t2 m6 = {.p=p2};
pthread_create(&tid2, NULL, func2, &m6);

/*
profession* p3;
profession_init(&p3,3);

main_t2 m7 = {.p=p3};

profession* p4;
profession_init(&p4,4);

main_t2 m8 = {.p=p4};





pthread_create(&tid2, NULL, func2, &m7);


pthread_create(&tid2, NULL, func2, &m8);

*/



worker* w1;
worker_init(&w1, fd, 1);
main_t m1 = {.fd = fd, .time = 10, .w = w1,.ed_store_blocknum=100};

receive_and_send_main(m1.w,m1.fd,m1.time,m1.ed_store_blocknum);



return 0;
}