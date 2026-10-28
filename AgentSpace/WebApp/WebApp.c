#include "foo.h"
#include "WebApp.h"
#include "../../Config/CWebConfig.h"

//=====================================================================
//  c_web 示例网站应用
//  静态页面来自 WebSite/ 目录（sendfile 零拷贝），动态接口在这里实现。
//
//  路由表（与 test/website_test.sh 的断言一一对应，共 14 条）：
//    GET  /  |  /index.html  -> WebSite/index.html      (text/html)
//    GET  /about | /about.html -> WebSite/about.html
//    GET  /style.css         -> WebSite/style.css       (text/css)
//    GET  /app.js            -> WebSite/app.js          (application/javascript)
//    GET  /favicon.ico       -> WebSite/favicon.ico     (image/x-icon)
//    GET  /hello             -> "Hello, c_web!"
//    GET  /time              -> "YYYY-MM-DD HH:MM:SS"
//    GET  /echo?name=&msg=   -> {"name":"..","msg":".."}   (先 URL 解码再 JSON 转义)
//    POST /api/echo          -> {"method":"POST","body":".."}；GET 该路由 -> 405
//    GET  /api/info          -> {"server":"c_web","pid":..,"uptime":..}
//    GET  /mem/256k          -> 262144 字节文本（超出池最大类，走 malloc 兜底）
//    GET  /download/10mb     -> WebSite/static/10mb.bin（sendfile，Content-Length 由 fstat 填）
//    其它未注册 URL          -> 框架统一回 404
//    静态路由上的非 GET 方法 -> 405（框架暂无 HEAD 语义，见报告 13.8）
//=====================================================================

// 网站根目录：相对进程工作目录。请在仓库根目录启动 ./main

#define MEM_BODY_SIZE (256 * 1024)
static char g_mem_body[MEM_BODY_SIZE + 1]; // 注册阶段填充，运行期只读（BSS，无并发写）
static time_t g_start_time = 0;

//------------------------------ 小工具 ------------------------------

static int MethodIs(Request* r, char* m)
{
    const char* got = RequestGetter(r, "Method", NULL);
    return got != NULL && strcmp(got, m) == 0;
}

// 查查询参数，缺省返回空串（避免业务里到处判 NULL）
static const char* QueryGet(Request* r, char* key)
{
    const char* v = RequestGetter(r, "Query", key);
    return v != NULL ? v : "";
}

// JSON 字符串转义：只处理必需字符，够示例使用
static void JsonEscape(char* dst, int cap, const char* src)
{
    int n = 0;
    if (cap <= 0) return;
    for (const char* p = src; *p != '\0' && n + 7 < cap; p++) {
        unsigned char c = (unsigned char)(*p);
        if (c == '"' || c == '\\') {
            dst[n++] = '\\';
            dst[n++] = (char)c;
        } else if (c == '\n') {
            dst[n++] = '\\';
            dst[n++] = 'n';
        } else if (c == '\r') {
            dst[n++] = '\\';
            dst[n++] = 'r';
        } else if (c == '\t') {
            dst[n++] = '\\';
            dst[n++] = 't';
        } else if (c < 0x20) {
            n += snprintf(dst + n, cap - n, "\\u%04x", c);
        } else {
            dst[n++] = (char)c;
        }
    }
    dst[n] = '\0';
}

// 十六进制字符 -> 数值，非法返回 -1（不依赖 ctype.h）
static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL 解码：框架 RequestGetter("Query",k) 给出的是原始串，这里补 %XX 与 '+' 的还原
static void UrlDecode(char* dst, int cap, const char* src)
{
    int n = 0;
    if (cap <= 0) return;
    for (const char* p = src; *p != '\0' && n < cap - 1; p++) {
        int hi = 0, lo = 0;
        if (*p == '%' && (hi = HexVal(p[1])) >= 0 && (lo = HexVal(p[2])) >= 0) {
            dst[n++] = (char)(hi * 16 + lo);
            p += 2;
        } else if (*p == '+') {
            dst[n++] = ' ';
        } else {
            dst[n++] = *p;
        }
    }
    dst[n] = '\0';
}

// 文本响应：Content-Type + 正文（正文须以 '\0' 结尾，框架按 strlen 处理）
static void SendText(Response* w, char* ctype, char* body)
{
    // 必须先设头：框架打包时要靠 headers->begin 计算长度，Content-Type 由应用决定
    if (ResponseHeaderSet(w, "Content-Type", ctype) != 1) return; // 头缓冲不足已降级 503
    if (body != NULL) ResponseBodySet(w, body);
}

// 文件响应：open 后交给框架 sendfile，发完由框架 close(fd)
static void SendFile(Response* w, char* path, char* ctype)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ResponseErrorSet(w, 404); // 文件缺失按 404 处理（启动目录不对时会看到）
        return;
    }
    if (ResponseHeaderSet(w, "Content-Type", ctype) != 1) {
        close(fd);
        return;
    }
    ResponseFileSet(w, fd, 0);
}

// 静态文件路由：统一只允许 GET
#define FILE_ROUTE(fn, relpath, ctype)                                        \
    static void fn(Request* r, Response* w)                                   \
    {                                                                         \
        if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }    \
        char path[512];                                                       \
        snprintf(path, sizeof(path), "%s%s", g_cfg.web_root, relpath);        \
        SendFile(w, path, ctype);                                             \
    }

FILE_ROUTE(HIndex,   "/index.html",      "text/html; charset=utf-8")
FILE_ROUTE(HAbout,   "/about.html",      "text/html; charset=utf-8")
FILE_ROUTE(HCss,     "/style.css",       "text/css; charset=utf-8")
FILE_ROUTE(HJs,      "/app.js",          "application/javascript; charset=utf-8")
FILE_ROUTE(HDown10M, "/static/10mb.bin", "application/octet-stream")
FILE_ROUTE(HFavicon, "/favicon.ico",     "image/x-icon")

//------------------------------ 动态接口 ------------------------------

// GET /hello
static void HHello(Request* r, Response* w)
{
    if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }
    SendText(w, "text/plain; charset=utf-8", "Hello, c_web!");
}

// GET /time -> "2026-10-14 14:45:59"
static void HTime(Request* r, Response* w)
{
    if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }

    char buf[32];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);

    SendText(w, "text/plain; charset=utf-8", buf);
}

// GET /echo?name=cline&msg=hello -> {"name":"cline","msg":"hello"}
// 查询串先做 URL 解码（支持中文/%xx/+），再做 JSON 转义
static void HEcho(Request* r, Response* w)
{
    if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }

    char raw[256];
    char name[256];
    char msg[256];
    char body[600];

    UrlDecode(raw, (int)sizeof(raw), QueryGet(r, "name"));
    JsonEscape(name, (int)sizeof(name), raw);
    UrlDecode(raw, (int)sizeof(raw), QueryGet(r, "msg"));
    JsonEscape(msg, (int)sizeof(msg), raw);

    snprintf(body, sizeof(body), "{\"name\":\"%s\",\"msg\":\"%s\"}", name, msg);

    SendText(w, "application/json; charset=utf-8", body);
}

// POST /api/echo（回显请求体）；同 URL 的 GET 请求 -> 405
static void HApiEcho(Request* r, Response* w)
{
    if (MethodIs(r, "POST") != 1) { ResponseErrorSet(w, 405); return; }

    const char* got = RequestGetter(r, "Body", NULL);
    char esc[512];
    char body[700];
    JsonEscape(esc, (int)sizeof(esc), got != NULL ? got : "");
    snprintf(body, sizeof(body), "{\"method\":\"POST\",\"body\":\"%s\"}", esc);

    SendText(w, "application/json; charset=utf-8", body);
}

// GET /api/info -> 服务自身信息（首页状态徽标用它）
static void HApiInfo(Request* r, Response* w)
{
    if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }

    char body[200];
    snprintf(body, sizeof(body),
             "{\"server\":\"c_web\",\"pid\":%d,\"uptime\":%ld,\"port\":%d}",
             (int)getpid(), (long)(time(NULL) - g_start_time), cweb_listen_port);

    SendText(w, "application/json; charset=utf-8", body);
}

// GET /mem/256k -> 262144 字节内存响应（超过内存池最大类，走 malloc 兜底）
static void HMem256k(Request* r, Response* w)
{
    if (MethodIs(r, "GET") != 1) { ResponseErrorSet(w, 405); return; }

    if (ResponseHeaderSet(w, "Content-Type", "text/plain; charset=utf-8") != 1) return;
    ResponseBodySet(w, g_mem_body); // strlen 到 g_mem_body[MEM_BODY_SIZE] 的 '\0' = 262144
}

//------------------------------ 注册入口 ------------------------------

static void Reg(char* url, Handler func)
{
    if (RouterAppend(url, func) != 1) printf("[WebApp] 路由注册失败：%s\n", url);
}

static void MemBodyBuild(void)
{
    const char* line = "c_web 256KB memory response test line 0123456789abcdefghijklmnopqrstuvwxyz\n";
    int len = (int)strlen(line);
    for (int i = 0; i < MEM_BODY_SIZE; i++) g_mem_body[i] = line[i % len];
    g_mem_body[MEM_BODY_SIZE] = '\0';
}

void WebAppRegisterRoutes(void)
{
    g_start_time = time(NULL);
    MemBodyBuild(); // 在启动线程之前准备好静态响应体，运行期只读

    // 静态网站
    Reg("/",            HIndex);
    Reg("/index.html",  HIndex);
    Reg("/about",       HAbout);
    Reg("/about.html",  HAbout);
    Reg("/style.css",   HCss);
    Reg("/app.js",      HJs);
    Reg("/favicon.ico", HFavicon);

    // 动态接口
    Reg("/hello",       HHello);
    Reg("/time",        HTime);
    Reg("/echo",        HEcho);
    Reg("/api/echo",    HApiEcho);
    Reg("/api/info",    HApiInfo);
    Reg("/mem/256k",    HMem256k);

    // 大文件（sendfile）
    Reg("/download/10mb", HDown10M);

    printf("[WebApp] 已注册 14 条路由，网站目录 %s/\n", g_cfg.web_root);
}
