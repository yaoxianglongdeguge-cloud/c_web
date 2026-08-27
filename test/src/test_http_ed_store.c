/* test_http_ed_store.c
 * 测试 HTTP 暂存区: 读入/完整请求切分/剩余数据保留/扩容
 * 重点验证: Http_ed_store_expend 扩容后 end 指针与内存块大小一致性
 */
#include "../../include.h"
#include <sys/wait.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

int main(void)
{
    printf("===== test_http_ed_store =====\n");

    Memory_Pool* pool = NULL;
    Memory_Pool_init(&pool, 10, 6, 500, 400);   /* 同 store_area: 1KB~512KB */

    http_ed_store* h = NULL;
    Http_ed_store_init(&h);
    CHECK(h != NULL, "http_ed_store init");

    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    /* ---------- 场景1: 正常读取+切分一个完整请求 ---------- */
    Http_ed_store_alloc(h, pool, 2);   /* 2KB */
    const char* req1 = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    write(sv[1], req1, strlen(req1));
    int r = Http_ed_store_write(h, sv[0]);
    CHECK(r == 1, "write 读到数据(返回1)");

    int state = 0, er = 0;
    char* target = http_state_judge(h, &state, &er);
    CHECK(state == 1 && er == 200, "状态机判定完整请求(state=1,er=200)");

    char out[256] = {0};
    Http_ed_store_copy(h, target, out);
    CHECK(strcmp(out, "GET / HTTP/1.1\r\nHost: x") == 0,
          "copy 切出的请求文本正确(不含分隔符\\r\\n\\r\\n)");
    CHECK(h->ptr_b == h->ptr_e, "切分后暂存区无残留数据");
    http_state_reset(h);

    /* ---------- 场景2: 一个 read 读到两个请求, 第二个请求残留应保留 ---------- */
    const char* req2 = "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
    write(sv[1], req2, strlen(req2));
    r = Http_ed_store_write(h, sv[0]);
    CHECK(r == 1, "write 读到两个请求的数据");

    char out2[256] = {0};
    target = http_state_judge(h, &state, &er);
    CHECK(state == 1, "第一个请求完整");
    Http_ed_store_copy(h, target, out2);
    CHECK(strcmp(out2, "GET /a HTTP/1.1\r\nHost: x") == 0, "第一个请求切分正确");
    CHECK(h->ptr_b != h->ptr_e, "暂存区有残留(第二个请求)");
    CHECK(h->ptr_b != NULL && strncmp(h->ptr_b, "GET /b", 6) == 0,
          "残留数据是第二个请求(GET /b 保留完好)");

    /* http_main 的真实流程: 每切出一个请求后要 http_state_reset 再判下一个 */
    http_state_reset(h);

    /* 残留再切一次 */
    target = http_state_judge(h, &state, &er);
    CHECK(state == 1, "残留请求判定完整");
    char out3[256] = {0};
    if (state == 1) {
        Http_ed_store_copy(h, target, out3);
        CHECK(strcmp(out3, "GET /b HTTP/1.1") == 0, "第二个请求切分正确");
    }
    http_state_reset(h);

    /* ---------- 场景3: 扩容后 end 指针一致性 (重点 bug) ---------- */
    {
        /* 重新分配 2KB, 填满 2047 字节(填到 end), 用合法请求前缀使其判定"不完整"而非报错 */
        Http_ed_store_free(h, pool);
        Http_ed_store_alloc(h, pool, 2);
        int cap = 2047;   /* 实际可填 = end-begin = 2*1024-1 */
        char* big = (char*)malloc(cap);
        memset(big, 'X', cap);
        memcpy(big, "GET /", 5);   /* 让方法匹配成功, 但没有 \r\n\r\n -> 不完整 */
        write(sv[1], big, cap);
        r = Http_ed_store_write(h, sv[0]);
        CHECK(r == 1 && h->ptr_e == h->end, "2KB 暂存区被填满(ptr_e==end)");

        target = http_state_judge(h, &state, &er);
        CHECK(state == -1, "未找到完整请求(判定不完整)");

        /* 触发扩容 */
        Http_ed_store_expend(h, pool);
        long long expect_end = (long long)(h->begin) + 4 * 1024 - 1;
        printf("       扩容后: end-begin = %lld (期望 4095), ptr_e-begin = %lld (期望 2047)\n",
               (long long)(h->end - h->begin), (long long)(h->ptr_e - h->begin));
        CHECK(h->end - h->begin == 4095,
              "扩容后 end-begin==4095(4KB块末端)  [当前代码可能为 3]");
        CHECK(h->ptr_e <= h->end,
              "扩容后 ptr_e 不越过 end  [当前代码 ptr_e=2047 > end=3]");

        /* 若状态已损坏, 再写数据会触发负数 VLA, 这里用子进程观察是否崩溃 */
        pid_t pid = fork();
        if (pid == 0) {
            char more[8] = "YYYYYYY";
            write(sv[1], more, 8);
            int rr = Http_ed_store_write(h, sv[0]);   /* length 可能为负 */
            _exit(rr == -1 || rr == 1 ? 0 : 2);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) {
            printf("[BUG ] 扩容后继续写入导致崩溃: 信号 %d (%s)\n",
                   WTERMSIG(st), strsignal(WTERMSIG(st)));
            failures++;
        } else {
            printf("[ OK ] 扩容后继续写入未崩溃\n");
        }
        free(big);
    }

    /* ---------- 清理 ---------- */
    Http_ed_store_destroy(h, pool);
    close(sv[0]); close(sv[1]);

    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}
