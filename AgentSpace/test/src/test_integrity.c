#include "../../../Include/Include.h"
#include <unistd.h>
#include <sys/wait.h>

// 模块内部函数（头文件未导出），为了单独验证解析逻辑在此显式声明
int MethodMatch(Request_Temp_Pool* hs, int* sit, char** method);
int BodyLength(int* len, char* bodylength);
int DealUrlAndQuery(Request_Analysiser* h, char* urlAndQuery, int* failReason);

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static Memory_Pool* g_pool;

// 在子进程里执行：函数直接崩溃（SIGSEGV）时父进程仍能记录并继续跑后续用例
static void run_isolated(const char* name, void (*fn)(void))
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);          // fn 内部用 _exit(2..) 表示结果不符预期
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) {
        g_fail++;
        printf("  [FAIL] %s：调用它的进程被信号 %d（%s）终止 → 函数本身崩溃\n",
               name, WTERMSIG(st), strsignal(WTERMSIG(st)));
    } else if (WEXITSTATUS(st) != 0) {
        g_fail++;
        printf("  [FAIL] %s：返回结果与预期不符（子进程退出码 %d）\n",
               name, WEXITSTATUS(st));
    } else {
        g_pass++;
        printf("  [PASS] %s\n", name);
    }
    fflush(stdout);
}

//-------- 用例 1：识别 HTTP 方法 --------
static void child_method_match(void)
{
    Request_Temp_Pool tp;
    memset(&tp, 0, sizeof(tp));
    tp.begin = (char*)"GET / HTTP/1.1\r\n\r\n";   // MethodMatch 只读 hs->begin

    int sit = 0;
    char* method = NULL;
    MethodMatch(&tp, &sit, &method);              // 原版首行 method=NULL → 此处必崩
    if (method == tp.begin && sit == 0) _exit(0);
    fflush(stdout);
    _exit(2);
}

//-------- 用例 2/3：Content-Length 数值解析 --------
// 契约（与 RequestIntergrityJudge 的调用方式一致）：传入 “Content-Length:” 行首指针，
// 由 BodyLength 内部自己跳过 15 字节的行名。调用方与函数合计必须只跳一次。
static void body_length_check(const char* line, int expect, const char* what)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", line);

    int len = -1;
    BodyLength(&len, buf);           // 传行首：BodyLength 内部 bodylength+15
    if (len == expect) _exit(0);
    printf("       %s：期望 %d，实际解析为 %d\n", what, expect, len);
    fflush(stdout);
    _exit(2);
}

static void child_body_length_small(void)
{
    body_length_check("Content-Length: 5\r\n\r\nhelloBODYBODYBODYBODYBODY", 5, "小请求体");
}

static void child_body_length_big(void)
{
    // > 1MB 必须解析出真实数值，否则 413 永远不会触发
    body_length_check("Content-Length: 2000000\r\n\r\nAAAAAAAAAAAAAAAAAAAAAAAA", 2000000, "超大请求体");
}

//-------- 用例 4：URL 与查询串拆分 --------
static void child_url_and_query(void)
{
    const char* raw = "/search?q=cline&page=2";
    int size = (int)strlen(raw);
    int allocsize = 4 * size + sizeof(Request_Analysiser) + 2 * sizeof(Analysiser_Hash)
                    + sizeof(Analysiser_Hash_Entry) * 14;

    Request_Analysiser* h = NULL;
    if (RequestAnalysisInit(&h, g_pool, allocsize) != 1) { fflush(stdout); _exit(9); }

    memcpy(h->ptr, raw, size + 1);
    char* text = h->ptr;
    h->ptr = h->ptr + size + 1;

    int err = 200;
    DealUrlAndQuery(h, text, &err);

    int e = 0;
    char* v = AnalysiserHashFind(h->Query, "q", &e);
    printf("       Url=%s，查询参数 q=cline → %s\n",
           h->Url ? h->Url : "(空)", (e == 1 && v != NULL) ? "命中" : "未命中");
    if (h->Url == NULL || strcmp(h->Url, "/search") != 0) { fflush(stdout); _exit(2); }
    if (!(e == 1 && v != NULL && strcmp(v, "cline") == 0)) { fflush(stdout); _exit(3); }

    v = AnalysiserHashFind(h->Query, "page", &e);
    if (!(e == 1 && v != NULL && strcmp(v, "2") == 0)) { fflush(stdout); _exit(4); }
    _exit(0);
}

//-------- 用例 5/6：空查询串 / 纯分隔符查询串（回归保护：BUG-05R ②）--------
// 语义：URL 里的 '?' 存在但后面没有有效键值对时，只应把 Url 截断为路径，
//       查询循环必须安全跳过（不得对 NULL 做 strchr）。
static void url_raw_check(const char* raw, const char* expect_url)
{
    int size = (int)strlen(raw);
    int allocsize = 4 * size + sizeof(Request_Analysiser) + 2 * sizeof(Analysiser_Hash)
                    + sizeof(Analysiser_Hash_Entry) * 14;

    Request_Analysiser* h = NULL;
    if (RequestAnalysisInit(&h, g_pool, allocsize) != 1) { fflush(stdout); _exit(9); }

    memcpy(h->ptr, raw, size + 1);
    char* text = h->ptr;
    h->ptr = h->ptr + size + 1;

    int err = 200;
    DealUrlAndQuery(h, text, &err);      // 当前实现：query_ing 为空 → strchr(NULL,'=') 崩溃

    int e = 0;
    char* v = (h->Query != NULL) ? AnalysiserHashFind(h->Query, "a", &e) : NULL;
    printf("       原始 url=[%s] → Url=[%s]，查询表=%s\n", raw, h->Url ? h->Url : "(空)",
           h->Query ? (v != NULL ? "含参数" : "空") : "未创建");
    if (h->Url == NULL || strcmp(h->Url, expect_url) != 0) { fflush(stdout); _exit(2); }
    if (v != NULL) { fflush(stdout); _exit(3); }   // 空查询串不应解析出任何参数
    _exit(0);
}

static void child_url_empty_query(void) { url_raw_check("/?", "/"); }
static void child_url_delim_only(void)  { url_raw_check("/?&", "/"); }

int main(void)
{
    printf("== test_integrity : 请求完整性判定（方法识别 / Content-Length / 查询串）==\n");

    CHECK(MemoryPoolInit(&g_pool, 1024*1024, 1) == 1, "初始化解析用内存池");

    run_isolated("MethodMatch 能识别 GET（不应崩溃）", child_method_match);
    run_isolated("Content-Length: 5 → 解析出 5", child_body_length_small);
    run_isolated("Content-Length: 2000000 → 解析出 2000000（413 判定依赖它）",
                 child_body_length_big);
    run_isolated("/search?q=cline&page=2 → Url=/search 且 q、page 都能查到",
                 child_url_and_query);
    run_isolated("空查询串 /? → 安全返回 Url=/（不应崩溃）", child_url_empty_query);
    run_isolated("纯分隔符 /?& → 安全返回 Url=/（不应崩溃）", child_url_delim_only);

    printf("---- test_integrity: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
