#include "../Include/Variate.h"
#include <stdint.h>

//扫描式内存池（位图 + 1KB 单位）
//  · 分配：找一段连续空闲单位（从 hint 开始按 64 位字扫描）
//  · 释放：把块覆盖的单位位清零 —— 空间【立刻】可复用，不需要"整池归零"
//  · free 不需要尺寸：分配时把"占几个单位"记在 lens[起点]
//  · 线程私有、无锁（和原来的池一样）
//API 与原 MemoryPool 完全一致，调用点不用改：
//    MemoryPoolInit(&p, perSize, entryNum)     // 总大小 = perSize * entryNum
//    MemoryPoolAlloc(p, needBytes, &ptr, &notfull)
//    MemoryPoolFree(p, ptr)

//请求内存池单次分配上限：<= 8KB 走池；> 8KB 直接 malloc
//  这样大请求（大 header / 1MB 上传体 / 大暂存扩容）不会把子池撑满，也不会因为找不到
//  连续空闲段而误判"池满"。free 时靠 big[] 判断这块是不是 malloc 来的。
#define MEMORY_POOL_MAX_POOLED (8*1024)

typedef struct Memory_Pool{

    char* arena;        //一次申请的大块
    int arenaSize;      //总字节
    int unitSize;       //单位字节（默认 1KB）
    int unitNum;        //单位个数

    uint64_t* bits;     //位图：1=占用，0=空闲
    uint32_t* lens;     //每个分配起点占几个单位（free 时靠它反推长度）

    int hint;           //上次分配后的位置（扫描起点）
    int usedUnits;      //在用单位数（统计）

    void** big;         //> 8KB、直接 malloc 的块（free 时先在这里找）
    int bigNum;
    int bigCap;

}Memory_Pool;

int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum);

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull);

int MemoryPoolFree(Memory_Pool* p,void* ptr);

//统计：在用字节、在用单位数、总单位数
int MemoryPoolStat(Memory_Pool* p,int* usedBytes,int* usedUnits,int* unitNum);
