#include "MemoryPool.h"
#include <stdlib.h>

/*
 * 过渡实现：MemoryPool 现在直接用 malloc/free。
 *
 * 每个分配块前面放一个头部，用于：
 *   1) free 时知道这块多大（统计 usedBytes 用）；
 *   2) 识别"不是本池分配的指针"，避免误 free 把进程打崩。
 *
 * 头部按 16 字节对齐，保证返回给调用方的指针对齐不低于 malloc 的保证。
 * 注意：和 malloc 一样，本实现【不能】可靠检测重复释放——块可能已被复用。
 *       调用方必须保证一次分配只释放一次。
 */

#define MP_HEADER_MAGIC 0x4D504F4F4C313233ULL   /* "MPOOL123" */

typedef struct Memory_Pool_Header {
    uint64_t magic;
    Memory_Pool* owner;
    size_t size;
    size_t pad;
} Memory_Pool_Header;

#define MP_HEADER_BYTES (((sizeof(Memory_Pool_Header) + 15u) / 16u) * 16u)

static Memory_Pool_Header* MpHeaderOf(void* ptr)
{
    return (Memory_Pool_Header*)((char*)ptr - MP_HEADER_BYTES);
}

int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum)
{
    if(p == NULL) return 0;

    //先置空：初始化失败时调用方拿到的必须是 NULL，而不是野指针
    *p = NULL;

    if(perSize <= 0 || entryNum <= 0) return 0;

    Memory_Pool* np = (Memory_Pool*)calloc(1,sizeof(Memory_Pool));
    if(np == NULL) return 0;

    //总配额只用于统计/调试：真正的分配上限由 malloc 决定
    long long total = (long long)perSize * (long long)entryNum;
    if(total <= 0){ free(np); return 0; }

    np->quotaBytes = (size_t)total;
    *p = np;

    return 1;
}

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull)
{
    if(notfull != NULL) *notfull = 0;
    if(ptr == NULL) return 0;

    *ptr = NULL;

    //池对象为空 / 非法大小：和旧实现一样视为分配失败
    if(p == NULL || needSize <= 0) return 0;

    size_t total = MP_HEADER_BYTES + (size_t)needSize;
    Memory_Pool_Header* h = (Memory_Pool_Header*)malloc(total);
    if(h == NULL) return 0;                 //只有真 OOM 才会走到这里

    h->magic = MP_HEADER_MAGIC;
    h->owner = p;
    h->size  = (size_t)needSize;
    h->pad   = 0;

    *ptr = (void*)((char*)h + MP_HEADER_BYTES);
    if(notfull != NULL) *notfull = 1;

    //统计：free 可能发生在另一个线程，用原子操作避免数据竞争
    size_t used = __atomic_add_fetch(&p->usedBytes,h->size,__ATOMIC_RELAXED);
    __atomic_add_fetch(&p->allocCount,1,__ATOMIC_RELAXED);

    size_t peak = __atomic_load_n(&p->peakBytes,__ATOMIC_RELAXED);
    while(used > peak &&
          !__atomic_compare_exchange_n(&p->peakBytes,&peak,used,0,
                                       __ATOMIC_RELAXED,__ATOMIC_RELAXED)){
        //peak 被其它线程改过：重试
    }

    return 1;
}

int MemoryPoolFree(Memory_Pool* p,void* ptr)
{
    if(ptr == NULL) return 1;               //和 free(NULL) 一致
    if(p == NULL) return 0;

    Memory_Pool_Header* h = MpHeaderOf(ptr);

    //归属校验：不是本池分配的指针直接拒绝（旧池也是这个语义，返回 0）
    if(h->magic != MP_HEADER_MAGIC || h->owner != p) return 0;

    size_t size = h->size;

    h->magic = 0;
    h->owner = NULL;

    __atomic_sub_fetch(&p->usedBytes,size,__ATOMIC_RELAXED);
    __atomic_add_fetch(&p->freeCount,1,__ATOMIC_RELAXED);

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

    //注意：这里只释放池对象本身；尚未归还的块会和 malloc 一样泄漏
    free(*p);
    *p = NULL;
}

#if 0   /* ---------------- 旧位图实现（保留作参考，不参与编译） ---------------- */

//把 [u, u+n) 的单位在位图里置 1（环形，允许跨尾）
/* 旧位图实现已移除；当前实现见本文件顶部（malloc 过渡实现） */

//把 [u, u+n) 的单位在位图里清 0（环形）
static void MemoryPoolBitsClear(Memory_Pool* p,int u,int n){

    for(int i=0;i<n;i++){

        int k = u + i;
        if(k >= p->unitNum) k -= p->unitNum;

        p->bits[k >> 6] &= ~(1ULL << (k & 63));
    }
}

//从 hint 开始找 need 个连续空闲单位（环形，按 64 位字扫描），返回起始单位或 -1
static int MemoryPoolFindRun(Memory_Pool* p,int need){

    int u = p->hint;
    int run = 0;
    int start = -1;
    int scanned = 0;

    while(scanned < p->unitNum){

        int w = u >> 6;
        int b = u & 63;

        //把 b 之前的位当成"已占用"，这样第一个空闲位一定 >= b
        uint64_t used = p->bits[w] | ((b == 0) ? 0ULL : ((1ULL << b) - 1));

        if(used == ~0ULL){                       //本字从 b 起全占，整字跳过

            int adv = 64 - b;
            u = (u + adv) % p->unitNum;
            scanned += adv;
            run = 0; start = -1;
            continue;
        }

        int first = __builtin_ctzll(~used);      //第一个空闲位
        int gap = first - b;

        if(gap > 0){                             //b 到 first 之间被占

            u = (u + gap) % p->unitNum;
            scanned += gap;
            run = 0; start = -1;
        }

        //从 first 起，本字内连续空闲的长度
        //注意：整字全空闲时 freem 为全 1，~freem==0，不能再喂给 ctz（UB）
        uint64_t freem = (~used) >> first;
        int len = (~freem) ? __builtin_ctzll(~freem) : (64 - first);

        if(run == 0) start = u;
        run += len;

        if(run >= need) return start;

        int endbit = first + len;

        scanned += len;
        u = (u + len) % p->unitNum;

        if(u == 0){                              //到 arena 末尾：不能和开头拼成一段
            run = 0; start = -1;
            continue;                            //从 0 继续扫（若还没扫满一圈）
        }

        if(endbit == 64) continue;               //没到末尾：跨字续接

        //下一位被占用：跳过它，并断开连续段
        u = (u + 1) % p->unitNum;
        scanned += 1;
        run = 0; start = -1;
    }

    return -1;
}

int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum){

    if(p == NULL || perSize <= 0 || entryNum <= 0) return 0;

    (*p) = malloc(sizeof(Memory_Pool));
    if((*p) == NULL) return 0;

    //总大小 = perSize * entryNum（保持和原来一样的调用方式）
    long total = (long)perSize * (long)entryNum;
    if(total <= 0){ free(*p); *p = NULL; return 0; }

    (*p)->arenaSize = (int)total;
    (*p)->unitSize  = (total >= 1024) ? 1024 : (int)total;
    (*p)->unitNum   = (int)(total / (*p)->unitSize);
    if((*p)->unitNum <= 0){ free(*p); *p = NULL; return 0; }

    (*p)->arena = malloc((size_t)(*p)->unitNum * (*p)->unitSize);
    (*p)->bits  = calloc(((*p)->unitNum + 63) / 64,sizeof(uint64_t));
    (*p)->lens  = calloc((*p)->unitNum,sizeof(uint32_t));
    (*p)->hint  = 0;
    (*p)->usedUnits = 0;
    (*p)->big   = NULL;
    (*p)->bigNum = 0;
    (*p)->bigCap = 0;

    if((*p)->arena == NULL || (*p)->bits == NULL || (*p)->lens == NULL){

        free((*p)->arena);
        free((*p)->bits);
        free((*p)->lens);
        free(*p);
        *p = NULL;
        return 0;
    }

    //最后一个 64 位字里超出 unitNum 的填充位标记为“已占用”。
    //否则扫描会把它们当成空闲单位，算出跨过 arena 末尾的“环形连续段”，
    //调用方按线性内存写就会越界。
    int padded = (((*p)->unitNum + 63) / 64) * 64;
    for(int k = (*p)->unitNum; k < padded; k++){
        (*p)->bits[k >> 6] |= 1ULL << (k & 63);
    }

    return 1;
}

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull){

    *notfull = 0;
    if(p == NULL || ptr == NULL || needSize <= 0) return 0;

    //超过 8KB：不从池里切，直接 malloc（登记一下，free 时能认出来）
    if(needSize > MEMORY_POOL_MAX_POOLED){

        void* m = malloc((size_t)needSize);
        if(m == NULL) return 0;

        if(p->bigNum >= p->bigCap){

            int cap = p->bigCap ? p->bigCap * 2 : 64;
            void** nb = realloc(p->big,(size_t)cap * sizeof(void*));
            if(nb == NULL){ free(m); return 0; }

            p->big = nb;
            p->bigCap = cap;
        }

        p->big[p->bigNum++] = m;

        *ptr = m;
        *notfull = 1;

        return 1;
    }

    int n = (needSize + p->unitSize - 1) / p->unitSize;   //需要几个单位
    if(n < 1) n = 1;
    if(n > p->unitNum) return 0;                          //比整个池还大

    int start = MemoryPoolFindRun(p,n);
    if(start < 0) return 0;                               //没有连续空闲段 → notfull=0
    if(start + n > p->unitNum) return 0;                  //只发线性块（环形段不能交给调用方）

    MemoryPoolBitsSet(p,start,n);
    p->lens[start] = (uint32_t)n;
    p->usedUnits += n;

    p->hint = start + n;
    if(p->hint >= p->unitNum) p->hint -= p->unitNum;

    *ptr = p->arena + (size_t)start * p->unitSize;
    *notfull = 1;

    return 1;
}

int MemoryPoolFree(Memory_Pool* p,void* ptr){

    if(p == NULL || ptr == NULL) return 1;

    //先看是不是 > 8KB 的 malloc 块
    if(p->bigNum > 0){

        for(int i=0;i<p->bigNum;i++){

            if(p->big[i] == ptr){

                p->big[i] = p->big[--p->bigNum];
                free(ptr);

                return 1;
            }
        }
    }

    char* q = (char*)ptr;
    if(q < p->arena || q >= p->arena + p->arenaSize) return 0;   //不是本池的指针
    if(((size_t)(q - p->arena) % (size_t)p->unitSize) != 0) return 0;

    int u = (int)((q - p->arena) / p->unitSize);
    int n = (int)p->lens[u];
    if(n <= 0) return 0;                                        //没分配过 / 重复释放

    MemoryPoolBitsClear(p,u,n);
    p->lens[u] = 0;
    p->usedUnits -= n;

    if(u < p->hint) p->hint = u;                                //低地址优先复用

    return 1;
}

int MemoryPoolStat(Memory_Pool* p,int* usedBytes,int* usedUnits,int* unitNum){

    if(p == NULL) return 0;

    if(usedBytes != NULL) *usedBytes = p->usedUnits * p->unitSize;
    if(usedUnits != NULL) *usedUnits = p->usedUnits;
    if(unitNum != NULL)   *unitNum = p->unitNum;

    return 1;
}

#endif  /* ---------------- 旧位图实现结束 ---------------- */

