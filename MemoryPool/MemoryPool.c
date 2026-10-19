#include "MemoryPool.h"

//把 [u, u+n) 的单位在位图里置 1（环形，允许跨尾）
static void MemoryPoolBitsSet(Memory_Pool* p,int u,int n){

    for(int i=0;i<n;i++){

        int k = u + i;
        if(k >= p->unitNum) k -= p->unitNum;

        p->bits[k >> 6] |= 1ULL << (k & 63);
    }
}

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

