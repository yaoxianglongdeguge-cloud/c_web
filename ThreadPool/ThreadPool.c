#include "../Include/Include.h"

typedef  struct main2{
    Worker_Thread* p;
} main_t2;

void* StartWorker(void* arg)
{
    main_t2* m=(main_t2*)arg;
    Worker_Thread* p=m->p;
    free(m);
    WorkerMain(p);
    return NULL;
}

char ThreadState(pid_t tid) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)tid);

    int fd = open(path, O_RDONLY);
    if (fd < 0) return '?';
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return '?';
    buf[n] = '\0';

    char *p = strrchr(buf, ')');
    return (p && p[1] == ' ') ? p[2] : '?';
}

int wakeThread(Worker_Thread* worker){

    pthread_mutex_lock(&(worker->workerMutex));
    atomic_store(&(worker->isMain),1);
    pthread_cond_signal(&(worker->workerCond));
    pthread_mutex_unlock(&(worker->workerMutex));  

    return 1;
}

int ThreadPoolInit(Receiver_Thread* receiver,int totalNum){
    if(totalNum <= 1){
        return 0;
    }
    receiver->threadPool = NULL;
    Thread_Pool* t = malloc(sizeof(Thread_Pool));
    t->totalNum = totalNum;
    t->workers = malloc(totalNum*sizeof(Worker_Thread*));
    pthread_t tid;
    for(int i = 0;i<totalNum;i++){
        
        WorkerThreadInit(&(t->workers[i]),i);
        t->workers[i]->taskQueue=receiver->taskQueue;
        main_t2* m2=(main_t2*)malloc(sizeof(main_t2));
        m2->p=t->workers[i];
        pthread_create(&tid, NULL, StartWorker, m2);
        
    }
    
    // 可选并行模式（worker.parallel）：默认只唤醒 worker[0]（1 主 + N 备）；
    // 为 1 时唤醒全部 worker，让同组多 worker 并行消费（压测/吞吐优先）
    if(g_cfg.worker_parallel){
        for(int i=0;i<totalNum;i++) wakeThread(t->workers[i]);
    }else{
        wakeThread(t->workers[0]);
    }
    t->mainThread = 0;
    t->nextWake = 1;
    receiver->threadPool = t;

    return 1;
}



int ThreadPoolChange(Receiver_Thread* receiver,int* isSuccess){
    
    *isSuccess = 0;
    Thread_Pool* t = receiver->threadPool;
    if(t == NULL) return 0;

    Worker_Thread* main = t->workers[t->mainThread];
    if(main->isOut == 0){ *isSuccess = 1; return 1; }
    char state = ThreadState((pid_t)atomic_load(&(main->tid)));
#ifdef CWEB_DEBUG_SWITCH
    fprintf(stderr,"[change] main=%d isOut=%d state=%c next=%d\n",
            t->mainThread, (int)main->isOut, state, t->nextWake);
#endif
    if (state == 'R') {  *isSuccess = 1; return 1; }
    if (state == 'S' || state == 'D') { 
        
        
        int i = 0;
        int k = t->nextWake;
        while(i < t->totalNum){
            if(t->workers[k]->isOut == 0){
                atomic_store(&(main->isMain),0);
                wakeThread(t->workers[k]);
                t->nextWake = (k+1)%(t->totalNum);
                t->mainThread = k;

                *isSuccess = 1;
                return 1;

            }else{
                i++;
                k = (k+1)%(t->totalNum);
            }
        }
    }

    *isSuccess = 1;
    return 1;


}