/* test_hash.c
 * 测试三个哈希表: hash_1(路由) / hash_2(请求头键值) / hash_3(fd表)
 */
#include "../../include.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

/* ---- hash_1: 路由表 ---- */
static void fake_handler_1(Request* r, Response* w) { (void)r; (void)w; }
static void fake_handler_2(Request* r, Response* w) { (void)r; (void)w; }

static void test_hash1(void)
{
    int err = 0;
    Hash_map* hm = Hash_Init(&err, 2);
    CHECK(err == 1 && hm != NULL, "hash1 init");

    /* 注意: hash1 只保存 key 指针不复制, 调用方需保证字符串存活(设计约定, 路由用静态字符串) */
    CHECK(Hash_Insert(&hm, hm, "/", fake_handler_1) == 1, "hash1 插入 /");
    CHECK(Hash_Insert(&hm, hm, "/user", fake_handler_2) == 1, "hash1 插入 /user");
    /* 重复插入: Entry_Insert 返回 0, 但 Hash_Insert 将其转换为 -1 */
    CHECK(Hash_Insert(&hm, hm, "/", fake_handler_2) == -1, "hash1 重复插入返回-1");

    Entry* e = Hash_Find(hm, "/user", &err);
    CHECK(err == 1 && e != NULL && e->value == fake_handler_2, "hash1 查 /user 命中");

    e = Hash_Find(hm, "/notexist", &err);
    CHECK(err != 1 || e == NULL, "hash1 查不存在返回失败");

    /* 扩容测试: 17桶 -> 负载>2 触发扩容, 插入后再查全部 (键需持久) */
    int n = 0;
    char* keys[70];
    for (int i = 0; i < 60; i++) {
        keys[i] = (char*)malloc(64);
        snprintf(keys[i], 64, "/route_%d", i);
        if (Hash_Insert(&hm, hm, keys[i], fake_handler_1) == 1) n++;
    }
    CHECK(n == 60, "hash1 批量插入60个");
    int hit = 1;
    for (int i = 0; i < 60; i++) {
        if (Hash_Find(hm, keys[i], &err) == NULL || err != 1) { hit = 0; break; }
    }
    CHECK(hit, "hash1 扩容后 60 个全部可查(验证扩容正确性)");
    for (int i = 0; i < 60; i++) free(keys[i]);
}

/* ---- hash_2: 请求头键值表 (内存来自 Http_analysis_1 块) ---- */
static void test_hash2(void)
{
    Http_analysis_1* h = (Http_analysis_1*)malloc(65536);
    h->size = 65536;
    h->ptr = (char*)h + sizeof(Http_analysis_1);
    h->end = (char*)h + h->size - 1;
    h->Method = h->Url = h->Version = h->Body = NULL;
    h->Query = h->Headers = NULL;

    Hash_map_2* m = (Hash_map_2*)h->ptr;
    CHECK(Hash2_Init(h, 1) == 1, "hash2 init");

    CHECK(Hash2_Insert(m, h, "Host", "www.baidu.com") == 1, "hash2 插入 Host");
    CHECK(Hash2_Insert(m, h, "User-Agent", "curl") == 1, "hash2 插入 UA");
    CHECK(Hash2_Insert(m, h, "Accept", "*/*") == 1, "hash2 插入 Accept");

    int err = 0;
    char* v = Hash2_Find(m, "Host", &err);
    CHECK(err == 1 && v != NULL && strcmp(v, "www.baidu.com") == 0, "hash2 查 Host 命中");
    v = Hash2_Find(m, "Accept", &err);
    CHECK(err == 1 && v != NULL && strcmp(v, "*/*") == 0, "hash2 查 Accept 命中");

    /* 插入 30 个键值验证链式扩展 (键/值需持久) */
    for (int i = 0; i < 30; i++) {
        char k[32], val[32];
        snprintf(k, sizeof(k), "H_%02d", i);
        snprintf(val, sizeof(val), "V_%02d", i);
        CHECK(Hash2_Insert(m, h, strdup(k), strdup(val)) == 1, "hash2 插入更多键值");
    }
    int hit = 1;
    for (int i = 0; i < 30; i++) {
        char k[32], val[32];
        snprintf(k, sizeof(k), "H_%02d", i);
        snprintf(val, sizeof(val), "V_%02d", i);
        v = Hash2_Find(m, k, &err);
        if (err != 1 || v == NULL || strcmp(v, val) != 0) { hit = 0; break; }
    }
    CHECK(hit, "hash2 30 个键值全部可查");

    /* 内存容量: 测试可插入上限(每节点占 sizeof(Hash2_Entry_2)) */
    int inserted = 0;
    for (int i = 0; i < 1500; i++) {
        char k[32], val[32];
        snprintf(k, sizeof(k), "B_%04d", i);
        snprintf(val, sizeof(val), "BV_%04d", i);
        if (Hash2_Insert(m, h, strdup(k), strdup(val)) != 1) break;
        inserted++;
    }
    printf("       hash2 64KB块可插入 %d 个键值(约%.1fKB)\n",
           inserted, (double)inserted * sizeof(Hash2_Entry_2) / 1024);
    CHECK(inserted > 100, "hash2 容量足够(>100个)");

    free(h);
}

/* ---- hash_3: fd 表 ---- */
static void test_hash3(void)
{
    Fd_Table* t = NULL;
    CHECK(Fd_Table_init(&t, 4) == 1, "hash3 (Fd_Table) init");

    for (int fd = 3; fd < 300; fd++) {
        if (Fd_Table_insert(t, fd) != 1) { CHECK(0, "hash3 批量插入"); break; }
    }
    int found = 0;
    Fd_Entry* e = NULL;
    for (int fd = 3; fd < 300; fd++) {
        if (Fd_Table_find(t, fd, &e) == 1 && e->fd == fd) found++;
    }
    CHECK(found == 297, "hash3 297 个 fd 全部可查");

    /* 删除一半 */
    int deleted = 0;
    for (int fd = 3; fd < 300; fd += 2) {
        if (Fd_Table_delete(t, fd) == 1) deleted++;
    }
    CHECK(deleted == 149, "hash3 删除 149 个成功");
    Fd_Entry* e2 = NULL;
    int rem_ok = 1;
    for (int fd = 4; fd < 300; fd += 2) {
        if (Fd_Table_find(t, fd, &e2) != 1) { rem_ok = 0; break; }
    }
    for (int fd = 3; fd < 300; fd += 2) {
        if (Fd_Table_find(t, fd, &e2) == 1) { rem_ok = 0; break; }
    }
    CHECK(rem_ok, "hash3 剩余偶数fd可查、已删奇数fd查不到");
}

int main(void)
{
    printf("===== test_hash =====\n");
    test_hash1();
    test_hash2();
    test_hash3();
    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}
