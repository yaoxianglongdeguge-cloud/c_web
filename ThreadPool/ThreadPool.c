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

char thread_state(pid_t tid) {
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

int ThreadPoolInit(Receiver_Thread* receiver,int totalNum){
    receiver->threadPool = NULL;
    Thread_Pool* t = malloc(sizeof(Thread_Pool));
    t->totalNum = totalNum;
    t->nextWake = 0;
    t->workers = malloc(totalNum*sizeof(Worker_Thread));
    for(int i = 0;i<totalNum;i++){

        WorkerThreadInit(&(t->workers[i]),i);
        t->workers[i]->taskQueue=receiver->taskQueue;
        main_t2* m2=(main_t2*)malloc(sizeof(main_t2));
        m2->p=t->workers[i];
        pthread_create(&tid, NULL, StartWorker, m2);
    
    }

    return 1;
}



int ThreadPoolChange(Receiver_Thread* receiver){

}