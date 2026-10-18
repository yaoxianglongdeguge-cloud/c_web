#include "../Include/Variate.h"

//分类空闲链内存池（线程私有，无锁）
//思路：按"块大小"把请求分成若干类，每类一条空闲链；free 即刻可复用，不需要整池归零。
//用法与 MemoryPool 一致：
//    ClassMemoryPoolInit(&p, 配额字节数, chunk 下限);
//    ClassMemoryPoolAlloc(p, needSize, &ptr, &notfull);
//    ClassMemoryPoolFree(p, ptr);            //不需要尺寸
//    ClassMemoryPoolDestroy(&p);

#define CLASS_POOL_MAX_CLASS   40      //最多多少个尺寸类
#define CLASS_POOL_ALIGN       16      //块大小对齐（>= sizeof(void*)，空闲时前 8B 放 next）
#define CLASS_POOL_BLK_MIN     32      //最小类
#define CLASS_POOL_BLK_MAX     131072  //最大类（超过的由调用方走大对象/413）
#define CLASS_POOL_CHUNK_CAP   (1024*1024)  //单个 chunk 上限

typedef struct Class_Memory_Class{

    int blkSize;        //本类的块大小（字节，已对齐）
    void* freelist;     //空闲块链头（链节点就是空闲块自己的前 8B）
    char* bump;         //当前 chunk 的分配游标
    char* bumpEnd;      //当前 chunk 的末尾
    int hadAllocNum;    //在用块数（统计用）

}Class_Memory_Class;

typedef struct Class_Memory_Chunk{

    int cls;            //本 chunk 服务的类下标
    char* begin;        //chunk 起始地址
    int blockNum;       //切成多少个块

}Class_Memory_Chunk;

typedef struct Class_Memory_Pool{

    char* arena;        //一次性申请的大块
    int arenaSize;      //大块总大小（= 配额）
    int arenaUsed;      //已经切出去多少字节（单调递增）
    int chunkMin;       //chunk 下限（小对象类的地板）
    Class_Memory_Chunk* chunks;     //chunk 表（地址单调递增，供 free 反查）
    int chunkNum;
    int chunkCap;
    Class_Memory_Class cls[CLASS_POOL_MAX_CLASS];
    int classNum;

}Class_Memory_Pool;

int ClassMemoryPoolInit(Class_Memory_Pool** p,int quota,int chunkMin);

int ClassMemoryPoolAlloc(Class_Memory_Pool* p,int needSize,void** ptr,int* notfull);

int ClassMemoryPoolFree(Class_Memory_Pool* p,void* ptr);

int ClassMemoryPoolDestroy(Class_Memory_Pool** p);

//统计：在用字节、chunk 数、类数、各 chunk 实际切出去的总字节
int ClassMemoryPoolStat(Class_Memory_Pool* p,int* usedBytes,int* chunkNum,int* classNum,int* arenaUsed);
