#include "../Include/Variate.h"

typedef struct Memory_Entry{

    char* beginPtr;
    char* allocPtr;
    int hadAllocNum;
    int freeSize;
    int totalSize;

}Memory_Entry;

typedef struct Memory_Pool{

    int perSize;
    int entryNum;
    char* beginPtr;
    char* endPtr;
    Memory_Entry* manageEntry;
    bool* freeEntry;
    int hint;

}Memory_Pool;


int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum);

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull);//非扩容分配

int MemoryPoolFree(Memory_Pool* p,void* ptr);