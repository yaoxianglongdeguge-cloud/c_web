#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

//Router.c 依赖的全局路由表对象
Router* router = NULL;

static void home(Request* r, Response* w) { (void)r; (void)w; }
static void hello(Request* r, Response* w) { (void)r; (void)w; }
static void download(Request* r, Response* w) { (void)r; (void)w; }

int main(void)
{
    printf("== test_router : URL → Handler 路由表 ==\n");

    router = (Router*)malloc(sizeof(Router));
    CHECK(router != NULL, "路由对象已创建");
    CHECK(RouterInit() == 1, "RouterInit 成功");
    CHECK(router->Hrt != NULL, "内部哈希表已建立");

    CHECK(RouterAppend("/", home) == 1, "注册 / ");
    CHECK(RouterAppend("/hello", hello) == 1, "注册 /hello");
    CHECK(RouterAppend("/download/10mb", download) == 1, "注册 /download/10mb");
    CHECK(router->Hrt->elem_num == 3, "路由表元素数 3");

    CHECK(RouterHandlerFind("/") == home, "查找 / 命中 home");
    CHECK(RouterHandlerFind("/hello") == hello, "查找 /hello 命中 hello");
    CHECK(RouterHandlerFind("/download/10mb") == download, "查找 /download/10mb 命中 download");

    CHECK(RouterHandlerFind("/not-exist") == NULL, "未注册 URL → NULL（框架回 404）");
    CHECK(RouterHandlerFind("/hell") == NULL, "前缀不匹配 → NULL（精确匹配）");
    CHECK(RouterHandlerFind("/hello?x=1") == NULL, "带查询串的原始 URL 不匹配（解析后已剥离）");

    CHECK(RouterAppend("/hello", home) == -1, "重复注册同一 URL → -1");
    CHECK(router->Hrt->elem_num == 3, "重复注册后元素数不变");

    printf("---- test_router: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
