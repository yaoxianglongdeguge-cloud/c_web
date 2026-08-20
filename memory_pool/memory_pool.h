#include <pthread.h> 
typedef struct Memory_Stack Memory_Stack;

typedef struct Memory_Entry Memory_Entry;

typedef struct Memory_Pool Memory_Pool;


typedef struct Memory_Stack{

    int* leisure;
    int top;
    pthread_mutex_t mutex;

}Memory_Stack;

typedef struct Memory_Entry{

    int strip_size;//单位是kb
    int strip_num;//条的数量
    int haded_num;//已被使用条数量
    void* memory_strip;
    Memory_Stack* stack;

}Memory_Entry;

typedef struct Memory_Pool{

    int Entry_num;
    Memory_Entry* pool; 

}Memory_Pool;


int Memory_Pool_init(Memory_Pool** p,int max_num,int init_max,int strip_num_future,int init_strip_num);//可以申请最大数量，但是只有前几个大小的初始化几条

int Memory_Pool_alloc(Memory_Pool* p,int size,void** ptr);

int Memory_Pool_free(Memory_Pool* p,void* ptr,int size);