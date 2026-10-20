#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static Memory_Pool* g_pool;

//模拟 WorkerThread::RequestGet：在业务线程内存池中分配解析结构，再解析原始请求文本
static Request_Analysiser* do_parse(const char* raw, int* error_reason)
{
    int size = strlen(raw);
    int allocsize = 4 * size + sizeof(Request_Analysiser) + 2 * sizeof(Analysiser_Hash)
                    + sizeof(Analysiser_Hash_Entry) * 14;
    Request_Analysiser* h = NULL;
    if (RequestAnalysisInit(&h, g_pool, allocsize) != 1) return NULL;

    memcpy(h->ptr, raw, size + 1); //连同结尾 '\0' 一起拷贝，和暂存区拷贝语义一致
    char* text = h->ptr;
    h->ptr = h->ptr + size + 1;

    int err = 200;
    RequestAnalysisReceive(h, text, &err);
    *error_reason = err;
    return h;
}

int main(void)
{
    printf("== test_parser : HTTP 请求头/URL/Query/Body 解析 ==\n");

    CHECK(MemoryPoolInit(&g_pool, 1024*1024, 1) == 1, "初始化业务线程内存池（1KB~64KB 类）");

    //-------- 1.带查询串与请求头的 GET --------
    int err = 200;
    const char* raw1 =
        "GET /search?q=cline&page=2 HTTP/1.1\r\n"
        "Host: 127.0.0.1:5343\r\n"
        "User-Agent: cweb-test/1.0\r\n"
        "\r\n";
    Request_Analysiser* h = do_parse(raw1, &err);
    CHECK(h != NULL, "解析结构分配成功");
    CHECK(err == 200, "正常请求 error_reason 保持 200");
    CHECK(strcmp(h->Method, "GET") == 0, "Method = GET");
    CHECK(strcmp(h->Url, "/search") == 0, "Url 已剥离查询串 → /search");
    CHECK(strcmp(h->Version, "HTTP/1.1") == 0, "Version = HTTP/1.1");

    int e2 = 0;
    char* v = AnalysiserHashFind(h->Query, "q", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "cline") == 0, "Query 参数 q=cline");
    v = AnalysiserHashFind(h->Query, "page", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "2") == 0, "Query 参数 page=2");
    v = AnalysiserHashFind(h->Headers, "Host", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "127.0.0.1:5343") == 0, "请求头 Host 解析正确");
    v = AnalysiserHashFind(h->Headers, "User-Agent", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "cweb-test/1.0") == 0, "请求头 User-Agent 解析正确");
    RequestAnalysisFree(h, g_pool);

    //-------- 2.POST + 请求体 --------
    const char* raw2 =
        "POST /api/echo HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    h = do_parse(raw2, &err);
    CHECK(err == 200 && strcmp(h->Method, "POST") == 0, "POST 请求解析成功");
    CHECK(strcmp(h->Url, "/api/echo") == 0, "Url = /api/echo");
    CHECK(h->Query == NULL, "无查询串时 Query 为 NULL");
    v = AnalysiserHashFind(h->Headers, "Content-Length", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "5") == 0, "Content-Length 头解析正确");
    CHECK(h->Body != NULL && strcmp(h->Body, "hello") == 0, "请求体内容 = hello");

    //请求头值前导空格被裁剪
    const char* raw3 =
        "GET /pad HTTP/1.1\r\n"
        "X-Space:    trimmed   \r\n"
        "\r\n";
    Request_Analysiser* h3 = do_parse(raw3, &err);
    v = AnalysiserHashFind(h3->Headers, "X-Space", &e2);
    CHECK(e2 == 1 && v != NULL && v[0] != ' ', "请求头值去掉前导空格");
    RequestAnalysisFree(h3, g_pool);
    RequestAnalysisFree(h, g_pool);

    //-------- 3.无查询串、无请求头 --------
    const char* raw4 = "GET / HTTP/1.0\r\n\r\n";
    Request_Analysiser* h4 = do_parse(raw4, &err);
    CHECK(err == 200, "极简请求解析成功");
    CHECK(strcmp(h4->Url, "/") == 0 && strcmp(h4->Version, "HTTP/1.0") == 0, "Url=/、Version=HTTP/1.0");
    CHECK(h4->Headers != NULL && h4->Headers->elem_num == 0, "无请求头时哈希表存在但为空");
    RequestAnalysisFree(h4, g_pool);

    //-------- 4.超长 URL → 414 --------
    const char* prefix = "GET /";
    size_t plen = strlen(prefix);
    int ulen = 8200;
    char* raw5 = (char*)malloc(plen + ulen + 16);
    memcpy(raw5, prefix, plen);
    memset(raw5 + plen, 'a', ulen);
    memcpy(raw5 + plen + ulen, " HTTP/1.1\r\n\r\n", 13);
    raw5[plen + ulen + 13] = '\0';
    Request_Analysiser* h5 = do_parse(raw5, &err);
    CHECK(err == 414, "URL 超过 8KB → 414");
    RequestAnalysisFree(h5, g_pool);
    free(raw5);

    //-------- 5.超长请求头 → 431 --------
    const char* hd = "GET /big HTTP/1.1\r\nX-Big: ";
    size_t hl = strlen(hd);
    int vlen = 17000;
    char* raw6 = (char*)malloc(hl + vlen + 16);
    memcpy(raw6, hd, hl);
    memset(raw6 + hl, 'b', vlen);
    memcpy(raw6 + hl + vlen, "\r\n\r\n", 4);
    raw6[hl + vlen + 4] = '\0';
    Request_Analysiser* h6 = do_parse(raw6, &err);
    CHECK(err == 431, "单个请求头超过 16KB → 431");
    RequestAnalysisFree(h6, g_pool);
    free(raw6);

    //-------- 6.重复请求头 → 400 --------
    const char* raw7 = "GET /dup HTTP/1.1\r\nX-Dup: 1\r\nX-Dup: 2\r\n\r\n";
    Request_Analysiser* h7 = do_parse(raw7, &err);
    CHECK(err == 400, "重复请求头 → 400（拒绝头部歧义）");
    RequestAnalysisFree(h7, g_pool);

    //-------- 7.查询串中无 '=' 的键被跳过 --------
    const char* raw8 = "GET /a?flag&x=1 HTTP/1.1\r\nHost: a\r\n\r\n";
    Request_Analysiser* h8 = do_parse(raw8, &err);
    CHECK(err == 200 && strcmp(h8->Url, "/a") == 0, "含无值参数的 URL 解析成功");
    v = AnalysiserHashFind(h8->Query, "x", &e2);
    CHECK(e2 == 1 && v != NULL && strcmp(v, "1") == 0, "有效查询参数 x=1 被解析");
    AnalysiserHashFind(h8->Query, "flag", &e2);
    CHECK(e2 == -1, "无 '=' 的查询项被跳过");
    RequestAnalysisFree(h8, g_pool);

    printf("---- test_parser: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
