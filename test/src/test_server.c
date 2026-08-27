/* test_server.c —— 服务器集成测试
 * 用真实的 Web_Driver 启动服务, 通过 TCP 客户端验证:
 *   1) 正常 GET -> 200   2) 路由未命中 -> 404   3) query 参数
 *   4) 管道化多请求保序  5) 半包(分片请求)完整处理  6) 空闲超时 408
 * 崩溃向量(在子进程中复现):
 *   7) 重复请求头 -> 解析失败路径   8) 超过 2KB 的大请求 -> 暂存区扩容
 */
#include "../../include.h"
#include <sys/wait.h>
#include <signal.h>

#define PORT_SRV  8899
#define PORT_CRASH1 8999
#define PORT_CRASH2 8998

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

/* ---------- 路由处理函数 ---------- */
static void handler_index(Request* req, Response* rsp)
{
    (void)req;
    Response_body_set(rsp, "hello-index");
    Response_header_set(rsp, "X-Test", "1");
}
static void handler_echo(Request* req, Response* rsp)
{
    const char* v = Request_get((Request*)req, "Query", "name");
    char buf[128];
    snprintf(buf, sizeof(buf), "echo:%s", v ? v : "(null)");
    Response_body_set(rsp, buf);
}
static void handler_big(Request* req, Response* rsp)
{
    (void)req;
    char* big = (char*)malloc(8193);
    memset(big, 'D', 8192);
    big[8192] = '\0';
    Response_body_set(rsp, big);   /* 8192 字节响应体 */
    free(big);
}
static void register_routes(void)
{
    Handler_append("/", handler_index);
    Handler_append("/echo", handler_echo);
    Handler_append("/big", handler_big);
}

/* ---------- 服务器线程入口 ---------- */
static void* server_entry(void* arg)
{
    int port = (int)(long)arg;
    Web_Driver("127.0.0.1", port, register_routes);
    return NULL;
}

/* ---------- TCP 客户端 ---------- */
static int tcp_connect(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    /* 设 2 秒接收超时, 避免异常场景挂死测试 */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}

/* 读一个完整 HTTP 响应: 解析头 + 按 Content-Length 读 body(或读到对端关闭) */
static int read_http_response(int fd, char* buf, int cap)
{
    int total = 0, hdr_end = -1;
    while (1) {
        if (total >= cap - 1) break;
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) break;                       /* EOF 或错误 */
        total += (int)n;
        buf[total] = '\0';
        char* p = strstr(buf, "\r\n\r\n");
        if (p) { hdr_end = (int)(p - buf) + 4; break; }  /* 头结束位置 */
    }
    if (hdr_end < 0) { buf[total] = '\0'; return total; }
    int body_len = 0;
    char* cl = strstr(buf, "Content-Length:");
    if (cl) body_len = atoi(cl + 15);
    while (total < hdr_end + body_len && total < cap - 1) {
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) break;
        total += (int)n;
    }
    buf[total] = '\0';
    return total;
}

/* ---------- 场景测试 ---------- */
static void test_normal_requests(void)
{
    char buf[8192];
    int fd = tcp_connect(PORT_SRV);
    CHECK(fd >= 0, "TCP 连接成功");
    if (fd < 0) return;

    const char* g1 = "GET / HTTP/1.1\r\nHost: t\r\n\r\n";
    send(fd, g1, strlen(g1), 0);
    int n = read_http_response(fd, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "HTTP/1.1 200 OK"), "GET / 返回 200");
    CHECK(strstr(buf, "hello-index") != NULL, "GET / 响应体正确");
    CHECK(strstr(buf, "X-Test: 1") != NULL, "自定义响应头 X-Test 存在");
    close(fd);

    fd = tcp_connect(PORT_SRV);
    const char* g2 = "GET /echo?name=cweb HTTP/1.1\r\nHost: t\r\n\r\n";
    send(fd, g2, strlen(g2), 0);
    n = read_http_response(fd, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "echo:cweb") != NULL, "GET /echo?name=cweb -> echo:cweb");
    close(fd);

    fd = tcp_connect(PORT_SRV);
    const char* g3 = "GET /nope HTTP/1.1\r\nHost: t\r\n\r\n";
    send(fd, g3, strlen(g3), 0);
    n = read_http_response(fd, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "404 Not Found") != NULL, "未知路由返回 404");
    close(fd);

    fd = tcp_connect(PORT_SRV);
    const char* g4 = "GET /big HTTP/1.1\r\nHost: t\r\n\r\n";
    send(fd, g4, strlen(g4), 0);
    n = read_http_response(fd, buf, sizeof(buf));
    CHECK(strstr(buf, "200 OK") != NULL && strstr(buf, "Content-Length: 8192") != NULL,
          "8KB 响应体 Content-Length 正确");
    close(fd);
}

static void test_pipelining_order(void)
{
    int fd = tcp_connect(PORT_SRV);
    CHECK(fd >= 0, "管道化: 连接成功");
    if (fd < 0) return;

    const char* reqs[] = {
        "GET /echo?name=first HTTP/1.1\r\nHost: t\r\n\r\n",
        "GET /echo?name=second HTTP/1.1\r\nHost: t\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: t\r\n\r\n",
    };
    for (int i = 0; i < 3; i++) send(fd, reqs[i], strlen(reqs[i]), 0);

    char buf[4096];
    int ok = 1;
    const char* expect[] = { "echo:first", "echo:second", "hello-index" };
    int got = 0;
    for (int i = 0; i < 3; i++) {
        int n = read_http_response(fd, buf, sizeof(buf));
        if (n <= 0) {
            printf("[INFO] 第%d个响应超时/未到达(收到 %d 个)\n", i, got);
            break;
        }
        got++;
        if (strstr(buf, expect[i]) == NULL) {
            printf("[FAIL] 第%d个响应不匹配: [%.80s]\n", i, buf);
            ok = 0;
            break;
        }
    }
    CHECK(ok && got == 3, "管道化 3 请求应收到 3 个有序响应");
    if (got < 3) {
        printf("       [提示] 已收到 %d/3 个响应 — 若请求丢失, 对应 http_main.c 中 serial 序号问题\n",
               got);
    }
    close(fd);
}

static void test_partial_request(void)
{
    /* 分片发送: 一半请求, 间隔, 再一半 —— 验证暂存区保留半包数据 */
    int fd = tcp_connect(PORT_SRV);
    CHECK(fd >= 0, "半包: 连接成功");
    if (fd < 0) return;

    const char* part1 = "GET /echo?name=half HTT";
    const char* part2 = "P/1.1\r\nHost: t\r\n\r\n";
    send(fd, part1, strlen(part1), 0);
    usleep(300 * 1000);              /* 300ms 后发剩余部分 */
    send(fd, part2, strlen(part2), 0);

    char buf[2048];
    int n = read_http_response(fd, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "echo:half") != NULL,
          "半包请求补全后正确响应(暂存区保留残留数据)");
    close(fd);
}

static void test_timeout(void)
{
    /* 连接后不发数据, 应收到 408 并断开 */
    int fd = tcp_connect(PORT_SRV);
    CHECK(fd >= 0, "超时: 连接成功");
    if (fd < 0) return;
    char buf[1024];
    int n = read_http_response(fd, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "408 Request Timeout") != NULL,
          "空闲连接超时返回 408");
    close(fd);
}

/* ---------- 崩溃向量测试(子进程) ---------- */
static void test_crash_duplicate_header(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        pthread_t th;
        pthread_create(&th, NULL, server_entry, (void*)(long)PORT_CRASH1);
        usleep(800 * 1000);
        int fd = tcp_connect(PORT_CRASH1);
        if (fd >= 0) {
            const char* dup = "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n";
            send(fd, dup, strlen(dup), 0);
            usleep(500 * 1000);
            close(fd);
        }
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV) {
        printf("[BUG ] 重复请求头导致服务器段错误崩溃(信号11)!\n");
        failures++;
    } else {
        printf("[ OK ] 重复请求头未崩溃(%s)\n",
               WIFSIGNALED(st) ? "被信号杀死" : "正常退出");
    }
}

static void test_crash_large_request(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        pthread_t th;
        pthread_create(&th, NULL, server_entry, (void*)(long)PORT_CRASH2);
        usleep(800 * 1000);
        int fd = tcp_connect(PORT_CRASH2);
        if (fd >= 0) {
            char req[4096];
            memset(req, 'A', sizeof(req));
            memcpy(req, "GET /", 5);
            req[3000] = '\r'; req[3001] = '\n';
            req[3002] = '\r'; req[3003] = '\n';
            req[3004] = '\0';
            send(fd, req, strlen(req), 0);
            usleep(500 * 1000);
            close(fd);
        }
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV) {
        printf("[BUG ] >2KB 大请求导致服务器段错误崩溃(信号11)!\n");
        failures++;
    } else {
        printf("[ OK ] >2KB 大请求未崩溃(%s)\n",
               WIFSIGNALED(st) ? "被信号杀死" : "正常退出");
    }
}

int main(void)
{
    printf("===== test_server (集成测试) =====\n");

    pthread_t srv;
    pthread_create(&srv, NULL, server_entry, (void*)(long)PORT_SRV);
    usleep(1000 * 1000);      /* 等服务器就绪 */

    test_normal_requests();
    test_pipelining_order();
    test_partial_request();
    test_timeout();
    test_crash_duplicate_header();
    test_crash_large_request();

    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}
