#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

//当前实现：MemoryPool 是 malloc/free 的薄封装（过渡实现，等自研池就绪后替换）
int main(void)
{
    printf("== test_memory_pool : malloc 过渡实现 ==\n");

    Memory_Pool* pool = NULL;
    CHECK(MemoryPoolInit(&pool, 1024*1024, 1) == 1, "MemoryPoolInit 返回 1");
    CHECK(pool != NULL, "内存池对象创建成功");

    void* p1 = NULL;
    void* p2 = NULL;
    void* p3 = NULL;
    int notfull = -1;

    //1. 正常分配：notfull 恒为 1，指针互不重叠
    CHECK(MemoryPoolAlloc(pool, 1, &p1, &notfull) == 1 && notfull == 1 && p1 != NULL,
          "1 字节分配成功且 notfull=1");
    CHECK(MemoryPoolAlloc(pool, 1024, &p2, &notfull) == 1 && notfull == 1 && p2 != NULL,
          "1KB 分配成功");
    CHECK(MemoryPoolAlloc(pool, 4096, &p3, &notfull) == 1 && notfull == 1 && p3 != NULL,
          "4KB 分配成功");
    CHECK(p1 != p2 && p2 != p3 && p1 != p3, "三次分配返回不同内存块");

    //2. 分配到的区域可以写满（检查没有越界/重叠）
    memset(p1, 0xAB, 1);
    memset(p2, 0xCD, 1024);
    memset(p3, 0xEF, 4096);
    CHECK(((unsigned char*)p1)[0] == 0xAB, "1 字节区域可写");
    CHECK(((unsigned char*)p2)[1023] == 0xCD, "1KB 区域可写满");
    CHECK(((unsigned char*)p3)[4095] == 0xEF, "4KB 区域可写满");

    //3. 统计
    int usedBytes = 0;
    int usedUnits = 0;
    int unitNum = 0;
    CHECK(MemoryPoolStat(pool, &usedBytes, &usedUnits, &unitNum) == 1, "MemoryPoolStat 返回 1");
    CHECK(usedBytes >= 1 + 1024 + 4096, "统计的在用字节覆盖三次分配");
    CHECK(unitNum > 0, "配置的配额已记录");

    //4. 归还后统计归零，且可以继续分配
    CHECK(MemoryPoolFree(pool, p1) == 1, "归还 1 字节块");
    CHECK(MemoryPoolFree(pool, p2) == 1, "归还 1KB 块");
    CHECK(MemoryPoolFree(pool, p3) == 1, "归还 4KB 块");
    CHECK(MemoryPoolStat(pool, &usedBytes, NULL, NULL) == 1 && usedBytes == 0,
          "全部归还后在用字节归零");

    void* again = NULL;
    CHECK(MemoryPoolAlloc(pool, 64, &again, &notfull) == 1 && again != NULL,
          "归还后可继续分配");

    //5. 大块（>8KB）不再有"池上限"，走普通 malloc
    void* big = NULL;
    CHECK(MemoryPoolAlloc(pool, 1024*1024, &big, &notfull) == 1 && big != NULL,
          "1MB 分配成功（没有池上限）");
    memset(big, 0x5A, 1024*1024);
    CHECK(((unsigned char*)big)[1024*1024 - 1] == 0x5A, "1MB 区域可写满");

    //6. 归属校验：别的池分配的指针，不能被本池释放
    Memory_Pool* pool2 = NULL;
    CHECK(MemoryPoolInit(&pool2, 64*1024, 1) == 1, "第二个池初始化成功");

    void* foreign = NULL;
    CHECK(MemoryPoolAlloc(pool2, 128, &foreign, &notfull) == 1 && foreign != NULL,
          "第二个池分配成功");
    CHECK(MemoryPoolFree(pool, foreign) == 0, "用错误的池释放 → 返回 0");
    CHECK(MemoryPoolFree(pool2, foreign) == 1, "由所属池释放 → 返回 1");

    //7. free(NULL) 与 malloc 语义一致
    CHECK(MemoryPoolFree(pool, NULL) == 1, "MemoryPoolFree(NULL) 视为成功");

    MemoryPoolFree(pool, again);
    MemoryPoolFree(pool, big);

    MemoryPoolDestroy(&pool);
    MemoryPoolDestroy(&pool2);
    CHECK(pool == NULL && pool2 == NULL, "Destroy 后指针置空");

    printf("---- test_memory_pool: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
