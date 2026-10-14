#include "../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

//分级内存池：按 2 的幂分级，非扩容语义（池满 notfull=0 由上层降级 503）
int main(void)
{
    printf("== test_memory_pool : 分级内存池（非扩容语义）==\n");

    //1.初始化：3 个大小类 1KB/2KB/4KB，每类预分配 2 条
    Memory_Pool* pool = NULL;
    int e = MemoryPoolInit(&pool, 3, 3, 10, 2);
    CHECK(e == 1, "MemoryPoolInit 返回 1");
    CHECK(pool != NULL, "内存池对象创建成功");
    CHECK(pool->Entry_num == 3, "大小类个数为 3");
    CHECK(pool->pool[0].strip_size == 1, "第 0 类粒度 1KB");
    CHECK(pool->pool[1].strip_size == 2, "第 1 类粒度 2KB");
    CHECK(pool->pool[2].strip_size == 4, "第 2 类粒度 4KB");
    CHECK(pool->pool[0].strip_num == 2, "第 0 类预分配 2 条");

    //2.正常分配 / 可写
    void* p1 = NULL;
    void* p2 = NULL;
    void* p3 = NULL;
    int notfull = -1;

    MemoryPoolAlloc(pool, 1, &p1, &notfull);
    CHECK(notfull == 1 && p1 != NULL, "1KB 分配成功且 notfull=1");
    MemoryPoolAlloc(pool, 1, &p2, &notfull);
    CHECK(notfull == 1 && p2 != NULL, "1KB 第二次分配成功");
    CHECK(p1 != p2, "两次分配返回不同内存块");

    memset(p1, 0xAB, 1024);
    CHECK(((unsigned char*)p1)[1023] == 0xAB, "分配到的 1KB 区域可写满");

    //3.池满降级：预分配 2 条，第三次必须 notfull=0
    MemoryPoolAlloc(pool, 1, &p3, &notfull);
    CHECK(notfull == 0, "超出预分配条数 → notfull=0（上层降级 503）");

    CHECK(MemoryPoolFree(pool, p1, 1024) == 1, "归还 1KB 内存块");
    MemoryPoolAlloc(pool, 1, &p3, &notfull);
    CHECK(notfull == 1 && p3 == p1, "归还后复用到同一块内存（栈式复用）");
    MemoryPoolFree(pool, p3, 1024);

    //4.按请求大小向上取整选类
    void* q1 = NULL;
    void* q2 = NULL;
    void* q3 = NULL;
    void* q4 = NULL;
    MemoryPoolAlloc(pool, 2, &q1, &notfull);
    CHECK(notfull == 1 && q1 != NULL, "2KB 请求命中第 1 类");
    MemoryPoolAlloc(pool, 4, &q2, &notfull);
    CHECK(notfull == 1 && q2 != NULL, "4KB 请求命中第 2 类");
    MemoryPoolAlloc(pool, 3, &q3, &notfull);
    CHECK(notfull == 1 && q3 != NULL, "3KB 请求向上取整到第 2 类（4KB）");
    MemoryPoolAlloc(pool, 4, &q4, &notfull);
    CHECK(notfull == 0, "第 2 类仅 2 条 → 第三次 notfull=0");

    CHECK(MemoryPoolFree(pool, q1, 2048) == 1, "按 2048 字节归还第 1 类");
    CHECK(MemoryPoolFree(pool, q2, 4096) == 1, "按 4096 字节归还第 2 类");
    CHECK(MemoryPoolFree(pool, q3, 3072) == 1, "按 3072 字节归还第 2 类");

    //5.超出最大类 → malloc 兜底，notfull 仍为 1
    void* big = NULL;
    MemoryPoolAlloc(pool, 8, &big, &notfull);
    CHECK(notfull == 1 && big != NULL, "8KB 超出最大类 → malloc 兜底");
    memset(big, 0x5A, 8 * 1024);
    CHECK(((unsigned char*)big)[8 * 1024 - 1] == 0x5A, "兜底内存可写满 8KB");
    CHECK(MemoryPoolFree(pool, big, 8 * 1024) == 1, "兜底内存按原始大小释放");

    //6.未预分配的大小类：直接降级，不扩容
    Memory_Pool* pool2 = NULL;
    MemoryPoolInit(&pool2, 3, 1, 10, 2); //只有第 0 类有 2 条，1KB/2KB/4KB 中的后两类为 0 条
    void* r1 = NULL;
    MemoryPoolAlloc(pool2, 2, &r1, &notfull);
    CHECK(notfull == 0, "未初始化条数的 2KB 类 → notfull=0");
    MemoryPoolAlloc(pool2, 1, &r1, &notfull);
    CHECK(notfull == 1 && r1 != NULL, "已初始化的 1KB 类仍可分配");

    //7.释放在池外的指针被拒绝
    int stack_var = 0;
    CHECK(MemoryPoolFree(pool, &stack_var, 1024) == 0, "释放非本池内存 → 返回 0");

    printf("---- test_memory_pool: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
