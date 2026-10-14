#include "../Include/Include.h"

typedef  struct main{
    Receiver_Thread* w;
    int fd;
    int time;
    int ed_store_blocknum;

} main_t;

typedef  struct main2{
    Worker_Thread* p;
} main_t2;

void* func1(void* arg)
{
    main_t* m=(main_t*)arg;
    ReveiverThreadMain(m->w,m->fd,m->time,m->ed_store_blocknum);
}
void* func2(void* arg)
{
    main_t2* m=(main_t2*)arg;
    while(1)
    {
    WorkerMain(m->p);
    }
}

int IgnoreSignal(){

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    return 1;
}

int DriveListen(int* Fd,char* IPaddr,int PORT){

    *Fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(*Fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(IPaddr);  
    addr.sin_port = htons(PORT);

    bind(*Fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(*Fd, 100);
    int flags = fcntl(*Fd, F_GETFL, 0);
    fcntl(*Fd, F_SETFL, flags | O_NONBLOCK);

    return 1;
}

int StartThread(int fd){
    pthread_t tid2;
    
    Worker_Thread* p1;
    WorkerThreadInit(&p1,1);
    main_t2 m5 = {.p=p1};
    
    pthread_create(&tid2, NULL, func2, &m5);

    Worker_Thread* p2;
    WorkerThreadInit(&p2,2);

    main_t2 m6 = {.p=p2};
    pthread_create(&tid2, NULL, func2, &m6);


    Receiver_Thread* w1;
    ReceiverThreadInit(&w1, fd, 1);
    main_t m1 = {.fd = fd, .time = 60, .w = w1,.ed_store_blocknum=100};

    ReveiverThreadMain(m1.w,m1.fd,m1.time,m1.ed_store_blocknum);

    return 1;

}

int WebStater(char* IPaddr,int PORT, RouterFunc Handler) 
{

    //忽略信号
    IgnoreSignal();

    //初始化全局资源，加载路由函数
    GlobalResourceInit(100,Handler);

    //启动监听
    int Fd=-1;
    DriveListen(&Fd,IPaddr,PORT);

    //开启线程
    StartThread(Fd);
    
    return 1;
    
}