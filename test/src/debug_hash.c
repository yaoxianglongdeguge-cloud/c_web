/* debug_hash.c —— 临时调试: 定位 hash1/hash2 插入失败原因 */
#include "../../include.h"

int main(void)
{
    /* ---- hash1 调试 ---- */
    printf("== hash1 ==\n");
    int err = 0;
    Hash_map* hm = Hash_Init(&err, 2);
    printf("init err=%d bu_num=%d\n", err, hm->bu_num);

    /* 先插 2 个 */
    printf("ins / -> %d\n", Hash_Insert(&hm, hm, "/", (Handler)1));
    printf("ins /user -> %d\n", Hash_Insert(&hm, hm, "/user", (Handler)2));
    printf("ins / (dup) -> %d  [注:Hash_Insert 把重复转换成 -1]\n", Hash_Insert(&hm, hm, "/", (Handler)3));

    /* 批量插, 打印失败点 */
    int fail_at = -1, n = 2;
    for (int i = 0; i < 60; i++) {
        char url[64];
        snprintf(url, sizeof(url), "/route_%d", i);
        int r = Hash_Insert(&hm, hm, url, (Handler)1);
        if (r != 1) { fail_at = i; printf("  insert#%d '%s' -> %d (bu_num=%d elem_num=%d)\n",
                    i, url, r, hm->bu_num, hm->elem_num); break; }
        n++;
    }
    if (fail_at == -1) printf("60 个全部插入成功\n");
    printf("最终 bu_num=%d elem_num=%d\n", hm->bu_num, hm->elem_num);

    /* 查第一个和最后一个 */
    Entry* e = Hash_Find(hm, "/route_0", &err);
    printf("查 /route_0 -> %s (err=%d)\n", e ? "命中" : "miss", err);
    e = Hash_Find(hm, "/route_59", &err);
    printf("查 /route_59 -> %s (err=%d)\n", e ? "命中" : "miss", err);

    /* ---- hash2 调试 ---- */
    printf("\n== hash2 ==\n");
    Http_analysis_1* h = (Http_analysis_1*)malloc(65536);
    h->size = 65536;
    h->ptr = (char*)h + sizeof(Http_analysis_1);
    h->end = (char*)h + h->size - 1;
    Hash_map_2* m = (Hash_map_2*)h->ptr;
    int r0 = Hash2_Init(h, 1);
    printf("init=%d bu_num=%d, ptr偏移=%ld end偏移=%ld\n", r0, m->bu_num,
           (long)(h->ptr - (char*)h), (long)(h->end - (char*)h));

    for (int i = 0; i < 30; i++) {
        char k[32], val[32];
        snprintf(k, sizeof(k), "H_%02d", i);
        snprintf(val, sizeof(val), "V_%02d", i);
        int r = Hash2_Insert(m, h, k, val);
        if (r != 1) {
            printf("  insert#%d '%s' -> %d (ptr偏移=%ld end偏移=%ld elem_num=%d)\n",
                   i, k, r, (long)(h->ptr - (char*)h), (long)(h->end - (char*)h), m->elem_num);
            break;
        }
    }
    printf("hash2 最终 elem_num=%d\n", m->elem_num);
    free(h);
    return 0;
}
