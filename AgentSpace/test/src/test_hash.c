#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static void h_route(Request* r, Response* w) { (void)r; (void)w; }
static void h_route2(Request* r, Response* w) { (void)r; (void)w; }

//哈希表：路由表 hash_1（DJB2+盐值，桶数组扩容）与请求解析表 hash_2（内存池内分配）
int main(void)
{
    printf("== test_hash : 路由哈希表 / 解析哈希表 ==\n");

    //------------ 1.RouterHash：插入 / 查找 / 重复 / 扩容 ------------
    int err = 0;
    Router_Hash* h = RouterHashInit(&err, 2); //init=2 → 17 个桶
    CHECK(err == 1 && h != NULL, "RouterHashInit 成功（17 桶）");
    CHECK(h->bu_num == 17, "初始桶数 17");
    CHECK(h->elem_num == 0, "初始元素数 0");

    CHECK(RouterHashInsert(&h, h, "/", h_route) == 1, "插入 / → h_route");
    CHECK(RouterHashInsert(&h, h, "/hello", h_route2) == 1, "插入 /hello → h_route2");
    CHECK(h->elem_num == 2, "元素数 2");

    Router_Hash_Entry* e1 = RouterHashFind(h, "/", &err);
    CHECK(err == 1 && e1 != NULL, "查找 / 命中");
    CHECK(e1->value == h_route, "  / 对应第一个处理函数");
    Router_Hash_Entry* e2 = RouterHashFind(h, "/hello", &err);
    CHECK(err == 1 && e2 != NULL && e2->value == h_route2, "查找 /hello 命中且函数正确");

    RouterHashFind(h, "/not-exist", &err);
    CHECK(err == -1, "查找不存在的 URL → Error=-1（上层返回 404）");

    CHECK(RouterHashInsert(&h, h, "/", h_route2) == -1, "重复注册同一 URL 被拒绝");
    CHECK(h->elem_num == 2, "重复注册不增加元素数");

    //100 个路由触发多次扩容后仍全部可查（桶大小不缩水、旧节点正确迁移）
    char urls[100][32];
    int all_ok = 1;
    for (int i = 0; i < 100; i++) {
        snprintf(urls[i], sizeof(urls[i]), "/api/item/%d", i);
        if (RouterHashInsert(&h, h, urls[i], h_route) != 1) all_ok = 0;
    }
    CHECK(all_ok == 1, "连续插入 100 个路由全部成功");
    CHECK(h->elem_num == 102, "元素数 102");
    CHECK(h->bu_num > 17, "元素数超过 2*桶数 → 触发扩容");

    int find_ok = 1;
    for (int i = 0; i < 100; i++) {
        Router_Hash_Entry* p = RouterHashFind(h, urls[i], &err);
        if (err != 1 || p == NULL || p->value != h_route) find_ok = 0;
    }
    CHECK(find_ok == 1, "扩容后 100 个路由仍全部可查（节点迁移正确）");

    //------------ 2.AnalysiserHash：请求头/查询键值表（内存池内） ------------
    Request_Analysiser* http = (Request_Analysiser*)malloc(sizeof(Request_Analysiser) + 8192);
    CHECK(http != NULL, "为解析哈希表申请 8KB 缓冲区");
    http->ptr = ((char*)http) + sizeof(Request_Analysiser);
    http->end = ((char*)http) + sizeof(Request_Analysiser) + 8192 - 1;

    CHECK(AnalysiserHashInit(http, 1) == 1, "AnalysiserHashInit 成功（7 桶）");
    Analysiser_Hash* hh = (Analysiser_Hash*)(((char*)http) + sizeof(Request_Analysiser));

    char k1[] = "Host";
    char v1[] = "127.0.0.1:5343";
    char k2[] = "User-Agent";
    char v2[] = "curl/8.5.0";
    CHECK(AnalysiserHashInsert(hh, http, k1, v1) == 1, "插入请求头 Host");
    CHECK(AnalysiserHashInsert(hh, http, k2, v2) == 1, "插入请求头 User-Agent");

    int e2v = 0;
    char* got = AnalysiserHashFind(hh, "Host", &e2v);
    CHECK(e2v == 1 && got != NULL && strcmp(got, "127.0.0.1:5343") == 0, "查找 Host 命中且值正确");
    got = AnalysiserHashFind(hh, "User-Agent", &e2v);
    CHECK(e2v == 1 && got != NULL && strcmp(got, "curl/8.5.0") == 0, "查找 User-Agent 命中");
    AnalysiserHashFind(hh, "X-Missing", &e2v);
    CHECK(e2v == -1, "查找不存在的键 → Error=-1");

    //------------ 3.RequestGetter 只读接口 ------------
    //Request 成员为 const，必须用初始化列表构造（与 WorkerThread 里一致）
    Request req = {
        .Method = "GET",
        .Url = "/echo",
        .Version = "HTTP/1.1",
        .Body = "hi",
        .Headers = hh,
        .Query = hh,
    };

    CHECK(strcmp(RequestGetter(&req, "Method", NULL), "GET") == 0, "RequestGetter 读 Method");
    CHECK(strcmp(RequestGetter(&req, "Url", NULL), "/echo") == 0, "RequestGetter 读 Url");
    CHECK(strcmp(RequestGetter(&req, "Version", NULL), "HTTP/1.1") == 0, "RequestGetter 读 Version");
    CHECK(strcmp(RequestGetter(&req, "Body", NULL), "hi") == 0, "RequestGetter 读 Body");
    CHECK(strcmp(RequestGetter(&req, "Headers", "Host"), "127.0.0.1:5343") == 0, "RequestGetter 读请求头");
    CHECK(RequestGetter(&req, "Headers", NULL) == NULL, "RequestGetter 缺键名 → NULL");
    CHECK(RequestGetter(&req, "Nope", NULL) == NULL, "RequestGetter 未知字段 → NULL");

    printf("---- test_hash: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
