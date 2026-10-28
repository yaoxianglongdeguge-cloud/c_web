#include "../Include/Include.h"


long long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

typedef  struct main{
    Receiver_Thread* w;
    int fd;
    int time;
    int ed_store_blocknum;

} main_t;

typedef  struct main2{
    Worker_Thread* p;
} main_t2;



//每组一个接待线程：参数从堆上取，取完就释放，不再依赖调用者的栈
void* func1(void* arg)
{
    main_t* m=(main_t*)arg;
    main_t m1=*m;
    free(m);
    ReveiverThreadMain(m1.w,m1.fd,m1.time,m1.ed_store_blocknum);
    return NULL;
}
void* func2(void* arg)
{
    main_t2* m=(main_t2*)arg;
    Worker_Thread* p=m->p;
    free(m);
    while(1)
    {
    WorkerMain(p);
    }
    return NULL;
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
    //每组一个监听 socket：SO_REUSEPORT 让内核按四元组把新连接分给各组的 socket，
    //避免多个接待线程抢同一个 listen fd（惊群 + 全被一个线程 accept 走）
    setsockopt(*Fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(IPaddr);  
    addr.sin_port = htons(PORT);

    if(bind(*Fd, (struct sockaddr*)&addr, sizeof(addr))!=0){
        close(*Fd);
        *Fd=-1;
        return -1;
    }
    if(listen(*Fd, g_cfg.listen_backlog)!=0){
        close(*Fd);
        *Fd=-1;
        return -1;
    }
    int flags = fcntl(*Fd, F_GETFL, 0);
    fcntl(*Fd, F_SETFL, flags | O_NONBLOCK);

    return 1;
}


int StartThread(char* IPaddr,int PORT){
    pthread_t tid;
    int group=g_cfg.group_num;
    int workers=g_cfg.worker_per_group;
    int started=0;
    
    printf("[WebStater] 组数=%d，每组接待线程=1，每组工作线程=%d\n",group,workers);
    fflush(stdout);
    
    for(int i=0;i<group;i++){
        
        //每组自己一个监听 socket（SO_REUSEPORT），连接由内核分摊到各组
        int fd=-1;
        if(DriveListen(&fd,IPaddr,PORT)!=1){
            printf("[WebStater] 第 %d 组监听失败（%s:%d）：bind/listen 出错\n",i+1,IPaddr,PORT);
            continue;
        }
        started++;
        
        //接待线程：任务队列是它私有的
        Receiver_Thread* w;
        ReceiverThreadInit(&w,fd,i+1,workers);
        
        main_t* m1=(main_t*)malloc(sizeof(main_t));
        m1->w=w;
        m1->fd=fd;
        m1->time=g_cfg.recv_idle_timeout_s;
        m1->ed_store_blocknum=g_cfg.send_pipeline_window;
        pthread_create(&tid, NULL, func1, m1);
    }
    
    if(started<=0) return -1;
    
    //各组的事件循环都在自己的线程里跑，当前线程只负责等（服务不返回）
    while(1) pause();
    
    return 1;
    
}

int WebStater(char* IPaddr,int PORT, RouterFunc Handler) 
{
    
    //忽略信号
    IgnoreSignal();

    if(CWebConfigInit(NULL)!=1) return -1;

    if(IPaddr!=NULL&&IPaddr[0]!='\0'&&CWebConfigIsSet("listen.addr")==0)
        snprintf(g_cfg.listen_addr,sizeof(g_cfg.listen_addr),"%s",IPaddr);

    if(PORT>0&&CWebConfigIsSet("listen.port")==0) g_cfg.listen_port=PORT;

    cweb_listen_port=g_cfg.listen_port;

    CWebConfigDump(&g_cfg);

    //初始化全局资源，加载路由函数
    GlobalResourceInit(Handler);
    
    //按组数启动：接待线程 + 工作线程，监听 socket 在 StartThread 里按组建
    if(StartThread(g_cfg.listen_addr,g_cfg.listen_port)!=1) return -1;
    
    return 1;
    
}

