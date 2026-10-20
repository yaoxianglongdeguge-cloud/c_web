#ifndef CWEB_MEMORY_POOL_H
#define CWEB_MEMORY_POOL_H

#include "../Include/Variate.h"
#include <stdint.h>
#include <stddef.h>

/*
 * 过渡实现：MemoryPool 现在直接用 malloc/free。
 *
 * 背景：位图 arena 版存在"跨尾误判池满""跨线程释放""重复释放靠 lens 兜底"等问题，
 *       正确性优先，先换回 libc 分配器；等吃透分配器原理后，只需替换本文件即可换回自研池。
 *
 * API 保持不变，上层调用点一行都不用改：
 *     MemoryPoolInit(&p, perSize, entryNum);      // 总配额 = perSize * entryNum
 *     MemoryPoolAlloc(p, needBytes, &ptr, &notfull);
 *     MemoryPoolFree(p, ptr);
 *
 * 与旧版位图池的语义差异：
 *   1) notfull 只在 malloc 失败（真 OOM）时为 0；正常恒为 1。
 *      => 不会再出现"池满 / 找不到连续段"导致的 503。
 *   2) 没有硬配额上限，quota 只用于统计和调试。
 *   3) MemoryPoolFree 的契约等同 free：ptr 必须是本 API 返回的，或 NULL；
 *      重复释放属于未定义行为（旧池靠 lens[u]==0 静默忽略，新实现不保证）。
 *   4) 线程安全：malloc/free 本身线程安全，统计计数使用原子操作。
 */

typedef struct Memory_Pool{

    size_t usedBytes;    //当前在用字节数（统计）
    size_t peakBytes;    //峰值（统计）
    size_t allocCount;   //累计分配次数
    size_t freeCount;    //累计释放次数
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
