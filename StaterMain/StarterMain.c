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

//内核允许的 listen backlog 上限（/proc 读不到返回 -1）
static int SysSomaxconn(void)
{
    FILE* f=fopen("/proc/sys/net/core/somaxconn","r");
    if(f==NULL) return -1;

    int v=-1;
    if(fscanf(f,"%d",&v)!=1) v=-1;
    fclose(f);

    return v;
}

int DriveListen(int* Fd,char* IPaddr,int PORT){

    *Fd = socket(AF_INET, SOCK_STREAM, 0);
    if(*Fd < 0){
        fprintf(stderr,"[Net] socket() 失败：errno=%d (%s)\n",errno,strerror(errno));
        *Fd=-1;
        return -1;
    }

    int opt = 1;
    if(setsockopt(*Fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))!=0)
        fprintf(stderr,"[Net] setsockopt(SO_REUSEADDR) 失败：errno=%d (%s)\n",errno,strerror(errno));
    //每组一个监听 socket：SO_REUSEPORT 让内核按四元组把新连接分给各组的 socket，
    //避免多个接待线程抢同一个 listen fd（惊群 + 全被一个线程 accept 走）
    if(setsockopt(*Fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))!=0)
        fprintf(stderr,"[Net] setsockopt(SO_REUSEPORT) 失败：errno=%d (%s)"
                       "；内核<3.9 时第 2 组起会 bind 失败，group.num 实际只能为 1\n",
                errno,strerror(errno));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(IPaddr);  
    addr.sin_port = htons(PORT);

    if(bind(*Fd, (struct sockaddr*)&addr, sizeof(addr))!=0){
        fprintf(stderr,"[Net] bind(%s:%d) 失败：errno=%d (%s)\n",IPaddr,PORT,errno,strerror(errno));
        close(*Fd);
        *Fd=-1;
        return -1;
    }
    if(listen(*Fd, g_cfg.listen_backlog)!=0){
        fprintf(stderr,"[Net] listen(backlog=%d) 失败：errno=%d (%s)\n",
                g_cfg.listen_backlog,errno,strerror(errno));
        close(*Fd);
        *Fd=-1;
        return -1;
    }
    int flags = fcntl(*Fd, F_GETFL, 0);
    if(flags<0 || fcntl(*Fd, F_SETFL, flags | O_NONBLOCK)!=0)
        fprintf(stderr,"[Net] 监听 socket 设置 O_NONBLOCK 失败：errno=%d (%s)\n",errno,strerror(errno));

    return 1;
}


int StartThread(char* IPaddr,int PORT){
    pthread_t tid;
    int group=g_cfg.group_num;
    int workers=g_cfg.worker_per_group;
    int started=0;
    
    printf("[WebStater] 组数=%d，每组接待线程=1，每组工作线程=%d，线程总数≈%d\n",
           group,workers,group*(1+workers)+1);
    fflush(stdout);

    //backlog 会被内核静默截断（somaxconn），先把真实上限打出来
    int smax=SysSomaxconn();
    if(smax>=0&&g_cfg.listen_backlog>smax)
        fprintf(stderr,"[Net] listen.backlog=%d 超过内核 somaxconn=%d，实际按 %d 生效\n",
                g_cfg.listen_backlog,smax,smax);
    
    for(int i=0;i<group;i++){
        
        //每组自己一个监听 socket（SO_REUSEPORT），连接由内核分摊到各组
        int fd=-1;
        if(DriveListen(&fd,IPaddr,PORT)!=1){
            fprintf(stderr,"[WebStater] 第 %d/%d 组监听失败（%s:%d）\n",i+1,group,IPaddr,PORT);
            continue;
        }
        started++;
        
        //接待线程：任务队列是它私有的
        Receiver_Thread* w;
        if(ReceiverThreadInit(&w,fd,i+1,workers)!=1){
            fprintf(stderr,"[WebStater] 第 %d/%d 组接待线程初始化失败，跳过该组\n",i+1,group);
            close(fd);
            started--;
            continue;
        }
        
        main_t* m1=(main_t*)malloc(sizeof(main_t));
        m1->w=w;
        m1->fd=fd;
        m1->time=g_cfg.recv_idle_timeout_s;
        m1->ed_store_blocknum=g_cfg.send_pipeline_window;
        int cr=pthread_create(&tid, NULL, func1, m1);
        if(cr!=0){
            fprintf(stderr,"[WebStater] 第 %d/%d 组接待线程创建失败：%d (%s)\n",
                    i+1,group,cr,strerror(cr));
            close(fd);
            started--;
        }
    }
    
    if(started<group)
        fprintf(stderr,"[WebStater] 只有 %d/%d 组启动成功，group.num=%d 未完全生效\n",
                started,group,group);

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

