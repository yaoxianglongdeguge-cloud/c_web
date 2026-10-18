#include "ClassMemoryPool.h"

#include "../Include/Include.h"

//向上取整到 CLASS_POOL_ALIGN
static int ClassPoolAlign(int size){

    return (size + CLASS_POOL_ALIGN - 1) & ~(CLASS_POOL_ALIGN - 1);
}

//构建尺寸阶梯：32B 起、1.25 倍、16B 对齐，再补上常用精确尺寸
//（2048 / 6144 / 65536 这些 c_web 实际在用的尺寸要正好落在类边界上）
static int ClassPoolLadderBuild(Class_Memory_Pool* p){

    int add[] = {1024,2048,4096,6144,8192,16384,32768,65536,131072};
    int i,j;

    //① 1.25 倍阶梯
    int size = CLASS_POOL_BLK_MIN;
    while(size <= CLASS_POOL_BLK_MAX && p->classNum < CLASS_POOL_MAX_CLASS - 9){

        p->cls[p->classNum].blkSize = size;
        p->cls[p->classNum].freelist = NULL;
        p->cls[p->classNum].bump = NULL;
        p->cls[p->classNum].bumpEnd = NULL;
        p->cls[p->classNum].hadAllocNum = 0;
        p->classNum++;

        int next = ClassPoolAlign(size + size / 4);
        if(next <= size) next = size + CLASS_POOL_ALIGN;
        size = next;
    }

    //② 补精确尺寸（去重后插入）
    for(i=0;i<9;i++){

        int v = add[i];
        int exist = 0;
        for(j=0;j<p->classNum;j++){
            if(p->cls[j].blkSize == v){ exist = 1; break; }
        }
        if(exist == 0 && p->classNum < CLASS_POOL_MAX_CLASS){

            p->cls[p->classNum].blkSize = v;
            p->cls[p->classNum].freelist = NULL;
            p->cls[p->classNum].bump = NULL;
            p->cls[p->classNum].bumpEnd = NULL;
            p->cls[p->classNum].hadAllocNum = 0;
            p->classNum++;
        }
    }

    //③ 按块大小升序（插入排序，类数很小）
    for(i=1;i<p->classNum;i++){

        Class_Memory_Class tmp = p->cls[i];
        j = i - 1;
        while(j >= 0 && p->cls[j].blkSize > tmp.blkSize){
            p->cls[j + 1] = p->cls[j];
            j--;
        }
        p->cls[j + 1] = tmp;
    }

    return p->classNum;
}

//needSize → 类下标（线性扫描，类数 <= CLASS_POOL_MAX_CLASS）
static int ClassPoolClassOf(Class_Memory_Pool* p,int needSize){

    for(int i=0;i<p->classNum;i++){
        if(p->cls[i].blkSize >= needSize) return i;
    }

    return -1;
}

//chunk 表扩容
static int ClassPoolChunkGrow(Class_Memory_Pool* p){

    int newCap = (p->chunkCap == 0) ? 64 : (p->chunkCap * 2);
    Class_Memory_Chunk* np = realloc(p->chunks,sizeof(Class_Memory_Chunk) * newCap);
    if(np == NULL) return 0;

    p->chunks = np;
    p->chunkCap = newCap;

    return 1;
}

//给第 ci 类切一个新 chunk（从 arena 顺序往后切，地址天然单调递增）
static int ClassPoolChunkNew(Class_Memory_Pool* p,int ci){

    Class_Memory_Class* c = &p->cls[ci];

    int size = c->blkSize * 16;
    if(size < p->chunkMin) size = p->chunkMin;
    if(size > CLASS_POOL_CHUNK_CAP) size = CLASS_POOL_CHUNK_CAP;
    if(size < c->blkSize) size = c->blkSize;
    size = size - (size % c->blkSize);          //必须是块大小的整数倍

    if(size <= 0) return 0;
    if(p->arenaUsed + size > p->arenaSize) return 0;   //配额用尽

    if(p->chunkNum >= p->chunkCap){
        if(ClassPoolChunkGrow(p) != 1) return 0;
    }

    char* begin = p->arena + p->arenaUsed;
    p->arenaUsed += size;

    p->chunks[p->chunkNum].cls = ci;
    p->chunks[p->chunkNum].begin = begin;
    p->chunks[p->chunkNum].blockNum = size / c->blkSize;
    p->chunkNum++;

    c->bump = begin;
    c->bumpEnd = begin + size;

    return 1;
}


//ptr 落在哪个 chunk（chunks 地址单调递增 → 二分）
static int ClassPoolChunkOf(Class_Memory_Pool* p,char* ptr){

    int lo = 0;
    int hi = p->chunkNum - 1;

    while(lo <= hi){

        int mid = (lo + hi) / 2;
        Class_Memory_Chunk* ch = &p->chunks[mid];
        Class_Memory_Class* c = &p->cls[ch->cls];
        char* end = ch->begin + (long)ch->blockNum * c->blkSize;

        if(ptr < ch->begin)      hi = mid - 1;
        else if(ptr >= end)      lo = mid + 1;
        else                     return mid;
    }

    return -1;
}

int ClassMemoryPoolInit(Class_Memory_Pool** p,int quota,int chunkMin){

    if(quota <= 0) return 0;

    (*p) = malloc(sizeof(Class_Memory_Pool));
    if((*p) == NULL) return 0;

    (*p)->arena = malloc(quota);
    if((*p)->arena == NULL){
        free(*p);
        *p = NULL;
        return 0;
    }

    (*p)->arenaSize = quota;
    (*p)->arenaUsed = 0;
    (*p)->chunkMin = (chunkMin > 0) ? chunkMin : (16 * 1024);
    (*p)->chunks = NULL;
    (*p)->chunkNum = 0;
    (*p)->chunkCap = 0;
    (*p)->classNum = 0;

    ClassPoolLadderBuild(*p);

    return 1;
}

int ClassMemoryPoolAlloc(Class_Memory_Pool* p,int needSize,void** ptr,int* notfull){

    *notfull = 0;
    if(p == NULL || ptr == NULL || needSize <= 0) return 0;

    int ci = ClassPoolClassOf(p,ClassPoolAlign(needSize));
    if(ci < 0) return 0;                       //超过最大类：由调用方走大对象/413

    Class_Memory_Class* c = &p->cls[ci];

    //① 空闲链命中：绝大多数情况走这里
    if(c->freelist != NULL){

        void* blk = c->freelist;
        c->freelist = *(void**)blk;
        c->hadAllocNum++;
        *ptr = blk;
        *notfull = 1;
        return 1;
    }

    //② 当前 chunk 还有空间：bump 一个块
    if(c->bump != NULL && c->bump + c->blkSize <= c->bumpEnd){

        void* blk = c->bump;
        c->bump += c->blkSize;
        c->hadAllocNum++;
        *ptr = blk;
        *notfull = 1;
        return 1;
    }

    //③ 领一个新 chunk
    if(ClassPoolChunkNew(p,ci) != 1) return 0;  //配额用尽 → notfull 保持 0

    void* blk = c->bump;
    c->bump += c->blkSize;
    c->hadAllocNum++;
    *ptr = blk;
    *notfull = 1;

    return 1;
}


int ClassMemoryPoolFree(Class_Memory_Pool* p,void* ptr){

    if(p == NULL || ptr == NULL) return 1;

    char* pt = (char*)ptr;
    if(pt < p->arena || pt >= p->arena + p->arenaSize) return 0;   //不是本池的指针

    int chi = ClassPoolChunkOf(p,pt);
    if(chi < 0) return 0;                                         //不在任何 chunk 里

    Class_Memory_Chunk* ch = &p->chunks[chi];
    Class_Memory_Class* c = &p->cls[ch->cls];

    long off = pt - ch->begin;
    if(off % c->blkSize != 0) return 0;                           //不是块首地址

    *(void**)pt = c->freelist;                                    //块前 8B 当 next
    c->freelist = pt;
    c->hadAllocNum--;

    return 1;
}

int ClassMemoryPoolStat(Class_Memory_Pool* p,int* usedBytes,int* chunkNum,int* classNum,int* arenaUsed){

    if(p == NULL) return 0;

    int used = 0;
    for(int i=0;i<p->classNum;i++){
        used += p->cls[i].hadAllocNum * p->cls[i].blkSize;
    }

    if(usedBytes != NULL) *usedBytes = used;
    if(chunkNum != NULL)  *chunkNum = p->chunkNum;
    if(classNum != NULL)  *classNum = p->classNum;
    if(arenaUsed != NULL) *arenaUsed = p->arenaUsed;

    return 1;
}

int ClassMemoryPoolDestroy(Class_Memory_Pool** p){

    if(p == NULL || *p == NULL) return 1;

    free((*p)->arena);
    free((*p)->chunks);
    free(*p);
    *p = NULL;

    return 1;
}

