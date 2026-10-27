#include "../Include/Variate.h"
#include <stddef.h>

#ifndef CWEB_MEMORY_POOL_H
#define CWEB_MEMORY_POOL_H


typedef struct Memory_Pool{

    size_t usedBytes;    //当前在用字节数（统计）
    size_t quotaBytes;   //初始化时配置的配额，仅用于统计/调试

}Memory_Pool;

int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum);

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull);

int MemoryPoolFree(Memory_Pool* p,void* ptr);

//统计：在用字节、在用 KB 数、配置的总 KB 数
int MemoryPoolStat(Memory_Pool* p,int* usedBytes,int* usedUnits,int* unitNum);

//释放池对象本身（尚未归还的块不会自动回收，和 malloc 语义一致）
void MemoryPoolDestroy(Memory_Pool** p);

#endif

