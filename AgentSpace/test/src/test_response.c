#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static Memory_Pool* g_pool; //模拟业务线程的 txt_pool

//按框架方式准备响应对象：头缓冲 64KB 类 + 默认状态 200 + 无文件
//注意 Response.pool 是 const 成员，只能在初始化列表里赋值
static Response rsp_make(void)
{
    void* ptr = NULL;
    int notfull = 0;
    //头缓冲按 64KB 申请：旧 API 的 63 表示"63KB 类"，新 API 单位是字节
    MemoryPoolAlloc(g_pool, 64*1024, &ptr, &notfull);

    Response r = {
        .error_reason = 200,
        .headers = (Response_Header_Entry*)ptr,
        .body = NULL,
        .body_size = 0,
        .send_fd = -1,
        .offset = 0,
        .pool = g_pool,
    };

    char* base = (char*)ptr + sizeof(Response_Header_Entry);
    r.headers->begin = base;
    r.headers->ptr = base;
    r.headers->end = base + 64512;

    return r;
}

int main(void)
{
    printf("== test_response : Response 构造接口 ==\n");

    CHECK(MemoryPoolInit(&g_pool, 1024*1024, 1) == 1, "初始化业务线程内存池（1KB~64KB）");

    Response r = rsp_make();

    //--------------- 文本响应 ---------------
    char body[] = "hello c_web";
    CHECK(ResponseBodySet(&r, body) == 1, "ResponseBodySet 设置文本响应体");
    CHECK(r.body_size == 11 && strcmp(r.body, "hello c_web") == 0, "响应体内容与长度一致");
    CHECK(r.error_reason == 200, "正常路径状态码仍为 200");

    //--------------- 响应头 ---------------
    char k1[] = "Content-Type";
    char v1[] = "text/plain; charset=utf-8";
    char k2[] = "X-Test";
    char v2[] = "1";
    CHECK(ResponseHeaderSet(&r, k1, v1) == 1, "ResponseHeaderSet 追加 Content-Type");
    CHECK(strcmp(r.headers->begin, "Content-Type: text/plain; charset=utf-8\r\n\r\n") == 0,
          "首个头部格式为 'K: v\\r\\n\\r\\n'（供框架 strcat 打包）");
    CHECK(ResponseHeaderSet(&r, k2, v2) == 1, "继续追加 X-Test");
    CHECK(strcmp(r.headers->begin, "Content-Type: text/plain; charset=utf-8\r\nX-Test: 1\r\n\r\n") == 0,
          "多个头部拼接后只有一个空行结尾");

    //--------------- 头缓冲写满 → 503 ---------------
    Response r2 = rsp_make();
    r2.headers->end = r2.headers->begin + 32; //人为缩小头缓冲
    CHECK(ResponseHeaderSet(&r2, k1, v1) == 0, "头缓冲不足 → 返回 0");
    CHECK(r2.error_reason == 503, "头缓冲不足 → 降级 503");

    //--------------- 错误码 ---------------
    CHECK(ResponseErrorSet(&r, 404) == 1 && r.error_reason == 404, "ResponseErrorSet 设置 404");
    r.error_reason = 200;

    //--------------- 大响应体：超出 64KB 最大类 → malloc 兜底 ---------------
    int big = 256 * 1024;
    char* text = (char*)malloc(big + 1);
    memset(text, 'A', big);
    text[big] = '\0';
    CHECK(ResponseBodySet(&r, text) == 1, "256KB 响应体写入成功（走 malloc 兜底）");
    CHECK(r.body_size == big, "大响应体长度正确");
    CHECK(r.body[0] == 'A' && r.body[big - 1] == 'A' && r.body[big] == '\0', "大响应体首尾完整且以 \\0 收尾");
    free(text);

    //--------------- 文件响应 ---------------
    int fd = open("README.md", O_RDONLY);
    if (fd < 0) fd = open("../README.md", O_RDONLY);
    CHECK(fd >= 0, "打开静态文件用于文件响应测试");
    CHECK(ResponseFileSet(&r, fd, 0) == 1, "ResponseFileSet 记录文件 fd 与偏移");
    CHECK(r.send_fd == fd && r.offset == 0, "send_fd / offset 字段正确（框架据此 sendfile）");

    struct stat st;
    fstat(fd, &st);
    CHECK(st.st_size > 0, "框架可通过 fstat 得到文件大小（写入 Content-Length）");
    close(fd);

    printf("---- test_response: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
