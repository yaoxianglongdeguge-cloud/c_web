/* test_memory_pool.c
 * 测试 memory_pool（新语义: 非扩容分配, 池满 notfull=0 由调用方降级）:
 *   分配/释放/复用 / 池满降级 / 超大尺寸走 malloc / 数据完整性
 */
#include "../../include.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

static int mem_is(const void* p, char ch, int n)
{
    const char* cp = (const char*)p;
    for (int i = 0; i < n; i++) if (cp[i] != ch) return 0;
    return 1;
}

int main(void)
{
    printf("===== test_memory_pool =====\n");

    Memory_Pool* p = NULL;
    /* 3 个大小类(1KB,2KB,4KB)，每类预分配 2 条 */
    Memory_Pool_init(&p, 3, 3, 4, 2);

    void *a = NULL, *b = NULL, *c = NULL, *e = NULL;
    int nf = 0;

    /* 1. 分配 2 条 1KB 成功 + 数据写入 */
    Memory_Pool_alloc(p, 1, &a, &nf);
    CHECK(nf == 1 && a != NULL, "alloc a 成功(notfull=1)");
    if (a) memset(a, 'A', 1024);
    Memory_Pool_alloc(p, 1, &b, &nf);
    CHECK(nf == 1 && b != NULL, "alloc b 成功(notfull=1)");
    if (b) memset(b, 'B', 1024);

    /* 2. 第 3 条: 非扩容语义 → 池满 notfull=0, 指针保持 NULL */
    Memory_Pool_alloc(p, 1, &c, &nf);
    CHECK(nf == 0 && c == NULL, "池满时 notfull=0(非扩容, 由调用方降级)");

    /* 3. 数据完整性(分配后未被破坏) */
    CHECK(mem_is(a, 'A', 1024), "a 数据完整(期望全'A')");
    CHECK(mem_is(b, 'B', 1024), "b 数据完整(期望全'B')");

    /* 4. 释放后复用 */
    Memory_Pool_free(p, a, 1024);
    Memory_Pool_alloc(p, 1, &e, &nf);
    CHECK(nf == 1 && e != NULL, "释放后复用成功(notfull=1)");
    if (e) { memset(e, 'E', 1024); CHECK(mem_is(e, 'E', 1024), "复用块 e 可写读"); }

    /* 5. 超大尺寸(>最大类 4KB)走 malloc 兜底 */
    void* big = NULL;
    Memory_Pool_alloc(p, 100, &big, &nf);
    CHECK(nf == 1 && big != NULL, "超大尺寸 malloc 兜底成功(notfull=1)");
    if (big) { memset(big, 'Z', 100 * 1024); CHECK(((char*)big)[0] == 'Z', "超大块可写读"); }
    Memory_Pool_free(p, big, 100 * 1024);

    /* 6. 不同大小类互不影响 */
    void* k2 = NULL;
    Memory_Pool_alloc(p, 2, &k2, &nf);   /* 2KB 类 */
    CHECK(nf == 1 && k2 != NULL, "2KB 类分配成功");
    if (k2) memset(k2, 'K', 2048);
    CHECK(mem_is(b, 'B', 1024), "分配 2KB 类不影响 1KB 类 b");
    Memory_Pool_free(p, k2, 2048);

    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}

