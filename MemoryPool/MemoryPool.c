#include "MemoryPool.h"
#include <stdlib.h>


typedef struct Memory_Pool_Header{
    Memory_Pool* owner;
    size_t       size;
}Memory_Pool_Header;

int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum)
{
    //perSize*entryNum 只是配额（统计/调试用），真正的上限就是物理内存
    if(p == NULL || perSize <= 0 || entryNum <= 0){
        fprintf(stderr,"[Mem] MemoryPoolInit 参数非法（perSize=%d, entryNum=%d）\n",perSize,entryNum);
        return 0;
    }

    //失败时 *p 正好是 NULL，调用方不会拿到野指针
    *p = (Memory_Pool*)calloc(1,sizeof(Memory_Pool));
    if(*p == NULL){
        fprintf(stderr,"[Mem] MemoryPoolInit 失败：calloc 失败（errno=%d，%s）\n",
                errno,strerror(errno));
        return 0;
    }

    (*p)->quotaBytes = (size_t)perSize * (size_t)entryNum;

    return 1;
}

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull)
{
    if(ptr == NULL) return 0;

    *ptr = NULL;
    if(notfull != NULL) *notfull = 0;

    if(p == NULL || needSize <= 0) return 0;

    Memory_Pool_Header* h = (Memory_Pool_Header*)malloc(sizeof(Memory_Pool_Header) + (size_t)needSize);
    if(h == NULL){
        //只有真 OOM 才会失败；限流 5 次，避免内存耗尽时刷屏
        static int oom_warned=0;
        if(oom_warned<5){
            oom_warned++;
            fprintf(stderr,"[Mem] 分配失败：needSize=%d errno=%d (%s)\n",
                    needSize,errno,strerror(errno));
        }
        return 0;
    }

    h->owner = p;
    h->size  = (size_t)needSize;

    *ptr = h + 1;
    if(notfull != NULL) *notfull = 1;

    //free 可能来自别的线程，统计用原子增减
    __atomic_add_fetch(&p->usedBytes,h->size,__ATOMIC_RELAXED);

    return 1;
}

int MemoryPoolFree(Memory_Pool* p,void* ptr)
{
    if(ptr == NULL) return 1;               //和 free(NULL) 一致
    if(p == NULL) return 0;

    Memory_Pool_Header* h = (Memory_Pool_Header*)ptr - 1;

    if(h->owner != p) return 0;             //不是本池发的块：拒绝

    __atomic_sub_fetch(&p->usedBytes,h->size,__ATOMIC_RELAXED);

    h->owner = NULL;
    free(h);

    return 1;
}

int MemoryPoolStat(Memory_Pool* p,int* usedBytes,int* usedUnits,int* unitNum)
{
    if(p == NULL) return 0;

    size_t used = __atomic_load_n(&p->usedBytes,__ATOMIC_RELAXED);

    if(usedBytes != NULL) *usedBytes = (int)used;
    if(usedUnits != NULL) *usedUnits = (int)((used + 1023u) / 1024u);
    if(unitNum   != NULL) *unitNum   = (int)(p->quotaBytes / 1024u);

    return 1;
}

void MemoryPoolDestroy(Memory_Pool** p)
{
    if(p == NULL || *p == NULL) return;

    //只释放池对象本身；没归还的块会和 malloc 一样泄漏
    free(*p);
    *p = NULL;
}
