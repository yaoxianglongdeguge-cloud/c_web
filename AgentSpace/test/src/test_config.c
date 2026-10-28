#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static int write_file(const char* path, const char* text)
{
    FILE* f = fopen(path, "w");
    if (f == NULL) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

int main(void)
{
    printf("== test_config : 配置文件解析（不读环境变量） ==\n");

    unsetenv("CWEB_CONF");   /* 配置值只来自文件，环境变量不参与 */

    char path[64];
    snprintf(path, sizeof(path), "/tmp/cweb_cfg_test_%d.conf", (int)getpid());

    /*--------------- 1. 默认值 ---------------*/
    CWebConfig c;
    CHECK(CWebConfigSetDefaults(&c) == 1, "SetDefaults 返回 1");
    CHECK(c.listen_port == 5343 && c.listen_backlog == 100, "监听默认值与旧硬编码一致");
    CHECK(c.worker_per_group == 4 && c.group_num == 1, "线程模型默认值与旧硬编码一致");
    CHECK(c.req_max_total == c.req_max_body + 64 * 1024, "max_total 默认 = max_body + 64KB");
    CHECK(CWebConfigValidate(&c) == 1, "默认值通过校验");

    /*--------------- 2. 文件覆盖 ---------------*/
    CHECK(write_file(path,
        "# 整行注释\n"
        "\n"
        "listen.port   =   8080   \r\n"
        "listen.backlog=512\n"
        "pool.recv_http_kb = 2048\n"
        "web.root = /var/www/cweb/\n"
        "listen.prot = 1234\n"
        "timer.capacity = 100\n"
        "timer.capacity = 200\n"
        "epoll.tick_ms = 5\n"
        "worker.parallel = 1\n"
        "request.max_total_bytes = 2000000\n") == 1, "写入合法配置文件");

    CHECK(CWebConfigInit(path) == 1, "合法配置 → Init 成功");
    CHECK(g_cfg.listen_port == 8080, "listen.port 生效（含空格/CRLF）");
    CHECK(g_cfg.listen_backlog == 512, "listen.backlog 生效");
    CHECK(g_cfg.pool_recv_http_kb == 2048, "pool.recv_http_kb 生效");
    CHECK(strcmp(g_cfg.web_root, "/var/www/cweb") == 0, "web.root 尾斜杠被归一化");
    CHECK(g_cfg.timer_capacity == 200, "重复键：后值覆盖前值");
    CHECK(g_cfg.epoll_tick_ms == 5, "epoll.tick_ms 生效");
    CHECK(g_cfg.req_max_total == 2000000, "request.max_total_bytes 生效");
    CHECK(CWebConfigIsSet("listen.port") == 1, "IsSet：文件里显式写过的键 → 1");
    CHECK(CWebConfigIsSet("listen.addr") == 0, "IsSet：文件里没写的键 → 0（WebStater 入参才生效）");
    CHECK(CWebConfigIsSet("no.such.key") == 0, "IsSet：未知键 → 0");
    CHECK(g_cfg.worker_parallel == 1, "worker.parallel = 1 生效");

    /*--------------- 3. 环境变量不再参与配置 ---------------*/
    setenv("CWEB_LISTEN_PORT", "9090", 1);
    setenv("CWEB_WORKER_PER_GROUP", "8", 1);
    setenv("CWEB_PARALLEL", "off", 1);
    CHECK(CWebConfigInit(path) == 1, "存在 CWEB_* 环境变量时 Init 仍成功");
    CHECK(g_cfg.listen_port == 8080, "CWEB_LISTEN_PORT 被忽略（仍用文件里的 8080）");
    CHECK(g_cfg.worker_per_group == 4, "CWEB_WORKER_PER_GROUP 被忽略（用默认 4）");
    CHECK(g_cfg.worker_parallel == 1, "旧变量 CWEB_PARALLEL 被忽略（仍用文件里的 1）");
    unsetenv("CWEB_LISTEN_PORT");
    unsetenv("CWEB_WORKER_PER_GROUP");
    unsetenv("CWEB_PARALLEL");

    /*--------------- 4. 非法输入 ---------------*/
    CHECK(write_file(path, "pool.recv_http_kb = abc\n") == 1, "写入非数值");
    CHECK(CWebConfigInit(path) == 0, "非数值 → Init 失败");

    CHECK(write_file(path, "listen.port = 8080abc\n") == 1, "写入带尾巴的数值");
    CHECK(CWebConfigInit(path) == 0, "数值带尾巴 → Init 失败");

    CHECK(write_file(path, "request.max_total_bytes = 1000\n") == 1, "写入过小的 max_total");
    CHECK(CWebConfigInit(path) == 0, "max_total < header+body → 校验失败");

    setenv("CWEB_EPOLL_TICK_MS", "abc", 1);
    CHECK(CWebConfigInit(NULL) == 1, "非法的 CWEB_EPOLL_TICK_MS 不再影响 Init（不读 env）");
    CHECK(g_cfg.epoll_tick_ms == 2, "epoll.tick_ms 仍为默认值 2");
    unsetenv("CWEB_EPOLL_TICK_MS");

    /*--------------- 5. 字符串边界 ---------------*/
    {
        char big[600];
        int n = snprintf(big, sizeof(big), "web.root = ");
        for (int i = 0; i < 300; i++) big[n++] = 'a';
        big[n++] = '\n';
        big[n] = '\0';
        CHECK(write_file(path, big) == 1, "写入超长字符串");
        CHECK(CWebConfigInit(path) == 0, "字符串超过 256 → Init 失败");
    }

    {
        CHECK(write_file(path, "web.root = /srv/a#b\n") == 1, "写入带 # 的路径");
        CHECK(CWebConfigInit(path) == 1 && strcmp(g_cfg.web_root, "/srv/a#b") == 0,
              "# 在值里被当作普通字符（只支持整行注释）");
    }

    /*--------------- 6. 缺文件 ---------------*/
    unsetenv("CWEB_CONF");
    CHECK(CWebConfigInit("/tmp/cweb_cfg_not_exist_zz") == 0, "显式路径不存在 → Init 失败");
    CHECK(CWebConfigInit(NULL) == 1, "不指定路径且默认文件不存在 → 用默认值启动");

    unlink(path);

    printf("---- test_config: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
