#include "../Include/Variate.h"

typedef struct Thread_Pool{
    int nextWake;
    int totalNum;
    Worker_Thread* workers;
}Thread_Pool;


int ThreadPoolInit(Receiver_Thread* receiver,int totalNum);