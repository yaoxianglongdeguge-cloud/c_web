#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static Memory_Pool* g_store; //模拟接收线程的 store_area
static int g_sv[2];          //socketpair 模拟客户端连接

static void feed(const char* data)
{
    write(g_sv[1], data, strlen(data));
}

//把一个请求暂存区耗尽（模拟 RequestComeMain 消费完一个请求后的重分配）
static void temp_pool_recycle(Request_Temp_Pool* tp)
{
    RequestTempPoolFree(tp, g_store);
    RequestTempPoolAlloc(tp, g_store, 2);
    RequestTempPoolStateReset(tp);
}

int main(void)
{
    CWebConfigInit(NULL);   /* 载入默认配置（测试里不读配置文件） */

    printf("== test_temp_pool : 请求暂存区 + 完整性判断（半包/粘包）==\n");

    CHECK(MemoryPoolInit(&g_store, 1024*1024, 1) == 1, "初始化 store_area 内存池");
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, g_sv) == 0, "建立模拟连接（socketpair）");

    Request_Temp_Pool* tp = NULL;
    CHECK(RequestTempPoolInit(&tp) == 1, "RequestTempPoolInit 成功");
    CHECK(tp->state_machine != NULL, "状态机已挂载到暂存区");
    CHECK(RequestTempPoolAlloc(tp, g_store, 2) == 1, "分配 2KB 暂存区");
    CHECK(tp->begin != NULL && tp->ptr_b == tp->begin && tp->ptr_e == tp->begin, "暂存区指针初始化正确");

    int state = 0, reason = 200;
    char* end = NULL;

    //-------- A.完整请求 --------
    const char* req1 = "GET /hello HTTP/1.1\r\nHost: a\r\n\r\n";
    int L1 = strlen(req1);
    feed(req1);
    CHECK(RequestTempPoolWrite(tp, g_sv[0]) == 1, "从连接读取请求数据");
    CHECK(tp->ptr_e - tp->ptr_b == L1, "有效数据长度 = 请求长度");

    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 1, "完整请求 → state=1");
    CHECK(end - tp->begin + 1 == L1 - 4, "拷贝末端指向请求尾（不含结尾 CRLFCRLF）");
    CHECK(tp->state_machine->h_rnrn == 1, "状态机记录已找到空行分隔符");

    void* store = NULL;
    int notfull = 0;
    MemoryPoolAlloc(g_store, 1024, &store, &notfull);
    CHECK(notfull == 1 && store != NULL, "在请求存储池申请 1KB 存放请求");
    CHECK(RequestTempPoolCopy(tp, end, (char*)store) == 1, "把整个请求拷出暂存区");
    CHECK(strncmp((char*)store, req1, L1 - 4) == 0, "拷出的请求文本正确");
    CHECK(tp->ptr_e == tp->begin && tp->ptr_b == tp->begin, "拷出后暂存区有效数据清空");
    RequestTempPoolStateReset(tp);
    CHECK(tp->state_machine->h_rnrn == 0 && tp->state_machine->h_method == 0, "状态机复位为初始态");

    //-------- B.半包：分两次到达 --------
    const char* half1 = "GET /half HTTP/1.1\r\nHost: b\r\n";
    feed(half1);
    RequestTempPoolWrite(tp, g_sv[0]);
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == -1, "请求不完整 → state=-1（继续等待数据）");
    CHECK(tp->state_machine->h_rnrn == 0, "半包时状态机记录“分隔符未找到”");

    feed("\r\n");
    RequestTempPoolWrite(tp, g_sv[0]);
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 1, "补齐剩余字节后 → state=1");
    CHECK(end - tp->begin + 1 == (int)(strlen(half1) + 2) - 4, "半包拼接后拷贝末端正确");
    temp_pool_recycle(tp);

    //-------- C.粘包：两个请求一次到达 --------
    const char* two = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
    feed(two);
    RequestTempPoolWrite(tp, g_sv[0]);
    char c1[64];
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 1, "粘包：先判定出第一个请求");
    CHECK(end - tp->begin + 1 == 15, "第一个请求拷贝长度 15（不含结尾空行）");
    RequestTempPoolCopy(tp, end, c1);
    CHECK(strcmp(c1, "GET /a HTTP/1.1") == 0, "第一个请求内容正确");
    CHECK(tp->ptr_e - tp->begin == 19, "剩余 19 字节完整保留（第二个请求）");
    RequestTempPoolStateReset(tp);
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 1, "粘包：第二个请求同样判定完整");
    RequestTempPoolCopy(tp, end, c1);
    CHECK(strcmp(c1, "GET /b HTTP/1.1") == 0, "第二个请求内容正确");
    CHECK(tp->ptr_e == tp->begin, "两个请求消费完，暂存区清空");
    temp_pool_recycle(tp);

    //-------- D.非法方法 → 400 --------
    feed("XXXX / HTTP/1.1\r\n\r\n");
    RequestTempPoolWrite(tp, g_sv[0]);
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 0 && reason == 400, "非法 HTTP 方法 → state=0 / 400");
    temp_pool_recycle(tp);

    //-------- E.PUT 缺少 Content-Length → 411 --------
    feed("PUT /x HTTP/1.1\r\nHost: a\r\n\r\n");
    RequestTempPoolWrite(tp, g_sv[0]);
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 0 && reason == 411, "PUT 无 Content-Length → 411（需要长度）");
    temp_pool_recycle(tp);

    //-------- F.请求体超过 1MB → 413 --------
    feed("POST /x HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n");
    RequestTempPoolWrite(tp, g_sv[0]);
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 0 && reason == 413, "Content-Length > 1MB → 413（请求体过大）");
    temp_pool_recycle(tp);

    //-------- G.带请求体的 POST：末端要覆盖整个请求体 --------
    const char* post = "POST /api HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    int Lp = strlen(post);
    feed(post);
    RequestTempPoolWrite(tp, g_sv[0]);
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp, &end, &state, &reason);
    CHECK(state == 1 && reason == 200, "带 5 字节请求体的 POST 判定完整");
    CHECK(end - tp->begin + 1 == Lp, "拷贝末端覆盖整个请求体（hello 也被拷出）");
    temp_pool_recycle(tp);

    //-------- H.请求超出 2KB 暂存区 → 扩容后继续接收 --------
    Request_Temp_Pool* tp2 = NULL;
    RequestTempPoolInit(&tp2);
    RequestTempPoolAlloc(tp2, g_store, 2);
    const char* head = "GET /expand HTTP/1.1\r\nX-Pad: ";
    size_t hl = strlen(head);
    int padlen = 3000;
    int total = hl + padlen + 4;
    char* big = (char*)malloc(total);
    memcpy(big, head, hl);
    memset(big + hl, 'p', padlen);
    memcpy(big + hl + padlen, "\r\n\r\n", 4);
    write(g_sv[1], big, total);

    CHECK(RequestTempPoolWrite(tp2, g_sv[0]) == 1, "首次读取填满 2KB 暂存区");
    CHECK(tp2->ptr_e == tp2->end, "暂存区已写满（ptr_e 到 end）");
    state = 0;
    reason = 200;
    RequestIntergrityJudge(tp2, &end, &state, &reason);
    CHECK(state == -1, "2KB 装不下该请求 → state=-1");

    int hadBigest = 0;
    CHECK(RequestTempPoolExpend(tp2, g_store, &hadBigest) == 1 && hadBigest == 0, "扩容成功（未到上限）");
    CHECK(tp2->end - tp2->begin + 1 == 4096, "暂存区由 2KB 扩为 4KB");
    RequestTempPoolWrite(tp2, g_sv[0]);
    RequestIntergrityJudge(tp2, &end, &state, &reason);
    CHECK(state == 1, "扩容并补齐剩余数据 → state=1");
    CHECK(end - tp2->begin + 1 == total - 4, "扩容后已接收数据完好（拷贝长度正确）");
    CHECK(strncmp(tp2->begin, "GET /expand HTTP/1.1", 20) == 0, "扩容后暂存区开头内容未损坏");
    RequestTempPoolFree(tp2, g_store);
    free(tp2);
    free(big);

    close(g_sv[0]);
    close(g_sv[1]);
    printf("---- test_temp_pool: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
