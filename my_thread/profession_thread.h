#include "../variate.h"

typedef struct profession{

    int id;

    Memory_Pool* txt_pool;
    Memory_Pool* http_pool;//http解析包
    Memory_Queue* memory_queue;//用来接收对方要释放内存的事件

}profession;

int profession_init(profession** profes,int id);
int deal_and_pack(profession* profes);