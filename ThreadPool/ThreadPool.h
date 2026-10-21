#include "../Include/Variate.h"

typedef struct Thread_Pool{
    int nextWake;
    int totalNum;
    int mainThread;
    Worker_Thread** workers;
}Thread_Pool;


int ThreadPoolInit(Receiver_Thread* receiver,int totalNum);

int ThreadPoolChange(Receiver_Thread* receiver,int* isSuccess);