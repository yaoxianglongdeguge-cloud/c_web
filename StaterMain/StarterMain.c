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
    if(listen(*Fd, 100)!=0){
        close(*Fd);
        *Fd=-1;
        return -1;
    }
    int flags = fcntl(*Fd, F_GETFL, 0);
    fcntl(*Fd, F_SETFL, flags | O_NONBLOCK);

    return 1;
}



//组数：一组 = 1 接待线程 + 1 条私有任务队列 + N 个工作线程
//  默认 1 组；压测/部署时用环境变量 CWEB_GROUP_NUM 调，不用改代码重编
static int ReceiverGroupNum(void){

    char* s=getenv("CWEB_GROUP_NUM");
    int n=(s!=NULL)?atoi(s):RECEIVER_GROUP_NUM_DEFAULT;

    if(n<1) n=1;
    if(n>RECEIVER_GROUP_NUM_MAX) n=RECEIVER_GROUP_NUM_MAX;

    return n;
}



static int WorkerThreadNum(void){

    char* s=getenv("CWEB_WORKER_NUM");
    int n=(s!=NULL)?atoi(s):WORKER_THREAD_NUM_DEFAULT;

    if(n<1) n=1;
    if(n>WORKER_THREAD_NUM_MAX) n=WORKER_THREAD_NUM_MAX;

    return n;
}


int StartThread(char* IPaddr,int PORT){
    pthread_t tid;
    int group=ReceiverGroupNum();
    int workers=WorkerThreadNum();
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
        ReceiverThreadInit(&w,fd,i+1,PER_RECEIVER_WORKER);
        
        main_t* m1=(main_t*)malloc(sizeof(main_t));
        m1->w=w;
        m1->fd=fd;
        m1->time=60;
        m1->ed_store_blocknum=100;
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
    
    //初始化全局资源，加载路由函数（任务队列容量也在这里配置）
    GlobalResourceInit(Handler);
    
    //按组数启动：接待线程 + 工作线程，监听 socket 在 StartThread 里按组建
    if(StartThread(IPaddr,PORT)!=1) return -1;
    
    return 1;
    
}

// static int WorkerThreadNum(void){

//     char* s=getenv("CWEB_WORKER_NUM");
//     int n=(s!=NULL)?atoi(s):WORKER_THREAD_NUM_DEFAULT;

//     if(n<1) n=1;
//     if(n>WORKER_THREAD_NUM_MAX) n=WORKER_THREAD_NUM_MAX;

//     return n;
// }