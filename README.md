# c_web —— 纯 C 实现的并发 HTTP 服务器框架

一个用纯 C 从零实现的并发 HTTP/1.1 服务器，以**静态库**形式提供给上层框架使用。
采用"收发线程 + 业务线程"的半同步/半异步模型，内置内存池、无锁化连接表、
HTTP 状态机解析、有序发送、断连定时器与跨线程内存所有权管理。

## 核心特性

- **半同步/半异步线程模型**：`Receiver_Thread`（收发线程，负责 accept / epoll / 读请求 / 发响应）
  + `Worker_Thread`（业务线程，负责 HTTP 解析 / 路由分发 / 构造响应），通过本组接待线程私有的任务队列解耦。
- **请求保序**：每个请求从读入起分配递增序号 `serial`，发送端用环形槽位
  `serial % blocknum` 保证同连接上响应顺序与请求顺序一致（乱序处理、有序发出）。
- **HTTP 状态机 + 分块暂存**：`Request_Temp_Pool` 处理拆包/粘包；`StateMachine` 判定请求完整性；
  请求超过暂存区大小时自动扩容。
- **内存分配**：当前为 `malloc/free` 过渡实现（保留原 `MemoryPool` API，便于后续换回自研池）；
  分配失败时 `notfull=0`，由调用方降级为 503；响应块经 `Worker_Queue` 归还原线程统一释放。
- **大文件零拷贝**：文件响应使用 `sendfile`，内存中响应支持 1MB 以内。
- **断连定时器**：小顶堆 + 线性扫描重置，空闲连接超时返回 408（默认 1 秒）。
- **路由表**：DJB2+盐值哈希，URL 精确匹配，支持启动时静态注册。
- **错误响应**：400/404/405/408/411/413/414/431/500/503 自动生成文本响应。

## 架构概览

```
        ┌──────────────────────────── WebStater(IP, PORT, Handler) ───────────────────────────┐
        │  ① 忽略 SIGPIPE → ② 初始化全局路由表/任务队列容量 → ③ 回调 Handler() 注册路由            │
        │  ④ 按组创建监听 socket → ⑤ 每组启动接待线程 + 工作线程                              │
        └──────────────────────────────┬───────────────────────────────────────────────────────┘
                                       │
                ┌──────────────────────┴───────────────────────┐
                │  Receiver_Thread ×N（每组一个，各自独立线程）          │
                │  · epoll 事件循环（10ms 超时）                │
                │  · accept / fd 表 / 定时器 / 有序发送         │
                └──────────────────────┬───────────────────────┘
                                       │ Task_Queue（每个接待线程私有，容量100，双缓冲）
                ┌──────────────────────▼───────────────────────┐
                │  Worker_Thread ×N（每组默认 2 个，业务处理）      │
                │  · HTTP 解析 / 路由分发 / 构造响应            │
                │  · 结果投回对应 Receiver_Thread 的发送队列             │
                └──────────────────────┬───────────────────────┘
                                       │ Receiver_Queue（每 Receiver_Thread 一个）
                ┌──────────────────────▼───────────────────────┐
                │  Receiver_Thread 发送端：按 serial 保序 write / sendfile│
                └──────────────────────┬───────────────────────┘
                                       │ Worker_Queue（响应内存归还给 Worker_Thread 释放）
                                       ▼
```

一次请求的生命周期：

```
accept → epoll → RequestComeMain 读入暂存区 → StateMachine 判定完整请求 → 复制到 http_pool
→ 投递 Task_Queue → Worker_Thread: 解析头部/URL/Query → RouterHandlerFind(url) → 业务函数写 Response
→ PackBackHead 打包 → Receiver_Queue → Receiver_Thread 按 serial 保序发送 → 内存经 Worker_Queue 归还
```

## 目录结构

| 目录/文件 | 职责 |
|---|---|
| `main.c` / `main.h` | `WebStater()` 启动驱动（框架入口） |
| `server/` | 全局资源：路由表 + 任务队列容量配置 |
| `my_thread/` | `worker_thread`（收发线程）、`profession_thread`（业务线程） |
| `connect_fd/` | 连接 fd 哈希表（fd → 暂存区/发送槽/序号） |
| `connect_config/` | 连接管理：accept / 统一关闭 `FdClose` |
| `http_analysis/` | HTTP 读入、暂存区、状态机、头部/URL/Query 解析 |
| `send_pointer_pool/` | 有序发送槽环形缓冲 + 发送主逻辑 |
| `queue/` | `task_queue`（全局任务）、`send_thing_queue`（发送事件）、`memory_queue`（内存回收） |
| `memory_pool/` | 分级内存池（非扩容，notfull 降级） |
| `Timer/` | 断连计时器（小顶堆 + `TimerFree` 移除） |
| `data_struct/` | hash_1 路由表 / hash_2 请求头键值 / hash_3 fd 表 / Prior_Queue_Timer 小顶堆 |
| `router/` | URL → Handler 路由 |
| `request_response/` | Request 只读接口、Response 构造接口 |
| `my_lock/` | 写优先读写锁（预置组件） |
| `test/` | 自动化测试（单元 + 集成 + valgrind） |


## 构建

```bash
make            # 编译可执行程序 main（需要自己提供 main() 入口，见下）
make lib        # 编译静态库 libcweb.a（推荐，框架以库形式使用）
make clean      # 清理目标文件/可执行/静态库
```

静态库包含 25 个模块（`ar t libcweb.a` 可查看），生成后链接方式：

```bash
gcc app.c -I<c_web路径> -L<c_web路径> -lcweb -lpthread -o app
```

> 注意：本仓库 `main.c` 只提供 `WebStater()` 启动驱动，**不含 `main()` 入口**，
> 由上层框架提供程序入口（服务器实例）。Makefile 的 `main` 目标仅为本地调试预留。

## 快速上手

```c
#include "include.h"

/* 业务处理函数：收到匹配请求时被调用 */
static void handler_home(Request* r, Response* w)
{
    (void)r;
    ResponseBodySet(w, "hello cweb");                    /* 写响应体 */
    ResponseHeaderSet(w, "Content-Type", "text/plain");  /* 自定义响应头 */
}

static void handler_echo(Request* r, Response* w)
{
    const char* name = RequestGetter(r, "Query", "name");    /* 取查询参数 */
    char buf[128];
    snprintf(buf, sizeof(buf), "echo:%s", name ? name : "(null)");
    ResponseBodySet(w, buf);
}

/* 路由注册回调：在监听 socket 建立前被 WebStater 调用 */
static void register_routes(void)
{
    RouterAppend("/", handler_home);
    RouterAppend("/echo", handler_echo);
}

int main(void)
{
    /* 阻塞运行：WebStater 会直接在当前线程跑 Receiver_Thread 事件循环 */
    WebStater("127.0.0.1", 8899, register_routes);
    return 0;
}
```

## 示例网站与在线接口

仓库自带一个可直接运行的示例站点：

| 目录/文件 | 说明 |
|---|---|
| `WebSite/` | 静态网站（`index.html`、`about.html`、`style.css`、`app.js`、`favicon.ico`、`static/10mb.bin`），由框架 `sendfile` 零拷贝下发 |
| `WebApp/WebApp.c` | 应用层：14 条路由的注册与 handler 实现（入口 `WebAppRegisterRoutes`） |
| `main.c` | `WebStater("127.0.0.1",5343,WebAppRegisterRoutes)`，启动即注册路由 |

```bash
make                 # 或 VS Code 的 build 任务 / F5（preLaunchTask 已包含 WebApp/WebApp.c）
./main               # 必须在仓库根目录启动：WebSite/ 是相对工作目录的路径
# 浏览器打开 http://127.0.0.1:5343
```

| 路由 | 方法 | 说明 |
|---|---|---|
| `/`、`/index.html`、`/about`、`/about.html` | GET | 静态页面（`text/html`） |
| `/style.css`、`/app.js`、`/favicon.ico` | GET | 静态资源 |
| `/hello` | GET | `Hello, c_web!` |
| `/time` | GET | 服务器本地时间 `YYYY-MM-DD HH:MM:SS` |
| `/echo?name=&msg=` | GET | 查询串回显 JSON（应用层做 URL 解码 + JSON 转义） |
| `/api/echo` | POST | 回显请求体 JSON；同 URL 的 GET 返回 **405** |
| `/api/info` | GET | 服务信息 JSON（pid / uptime / port） |
| `/mem/256k` | GET | 262144 字节内存响应（超过内存池最大类，走 malloc 兜底） |
| `/download/10mb` | GET | 10MiB 文件 `sendfile` 下载（`Content-Length` 由框架 fstat 自动填） |
| 未注册路径 | 任意 | 框架统一回 **404 Not Found** |

应用层写法（`WebApp/WebApp.c`）：

```c
static void HHello(Request* r, Response* w)          // 签名：void (*)(Request*, Response*)
{
    if (strcmp(RequestGetter(r, "Method", NULL), "GET") != 0) {
        ResponseErrorSet(w, 405);                    // 方法不符
        return;
    }
    ResponseHeaderSet(w, "Content-Type", "text/plain; charset=utf-8"); // 应用负责 Content-Type
    ResponseBodySet(w, "Hello, c_web!");             // Content-Length 由框架打包时生成
}
// 文件响应：ResponseFileSet(w, open(path, O_RDONLY), 0);  发完由框架 close
```

端到端验证（与单元测试一样，退出码即结论）：

```bash
make -C AgentSpace/test run              # 单元 + 组件级（12 个用例，退出码即结论）
bash AgentSpace/test/website_test.sh     # 网站端到端（含 10MB 下载与 4 路并发）
```

> 已知限制：框架暂不支持 HEAD 语义（`HEAD /` 会命中 405）；查询串不做 URL 解码（示例在应用层补了 `UrlDecode`）；
> 静态文件路径是相对工作目录的。详见 `AgentSpace/test/TEST_REPORT.md` 第 13 章。

## API 参考

### 启动

```c
int WebStater(char* IPaddr, int PORT, HandlerFunc Handler);
```

- `Handler`：`void (*)(void)` 回调，在 socket 建立**之前**执行，用于 `RouterAppend` 注册路由。
- 内部流程：忽略 SIGPIPE → 初始化全局路由表与任务队列容量 → 回调注册路由 →
  按 `CWEB_GROUP_NUM` 建组（每组一个 `SO_REUSEPORT` 监听 socket）→ 每组启动 2 个 Worker_Thread
  （`CWEB_WORKER_NUM` 可调）→ **各组接待线程在自己的线程里跑事件循环，当前线程阻塞等待**。

### 路由

```c
typedef void (*Handler)(Request*, Response*);
int RouterAppend(char* url, Handler func);   // 注册 URL → 处理函数（静态精确匹配）
Handler RouterHandlerFind(char* url);               // 查找（未命中返回 NULL → 404）
```

### 请求（只读）

```c
typedef struct Request {
    const char* const Method;      // "GET" / "POST" ...
    const char* const Url;         // 去掉查询串的路径，如 "/echo"
    const char* const Version;     // "HTTP/1.1"
    const Analysiser_Hash* const Query; // 查询参数键值表
    const Analysiser_Hash* const Headers;// 请求头键值表
    const char* const Body;        // 请求体
} Request;

const char* const RequestGetter(Request* r, "Method"|"Url"|"Version"|"Body", NULL);
const char* const RequestGetter(Request* r, "Headers"|"Query", "键名");  // 查头/查参数
```

### 响应

```c
int ResponseBodySet(Response* r, char* c);      // 设置文本响应体（自动分配内存）
int ResponseHeaderSet(Response* r, char* key, char* value);  // 追加自定义头
int ResponseFileSet(Response* r, int fd, off_t offset);        // 文件响应（sendfile 零拷贝）
int ResponseErrorSet(Response* r, int error);                 // 设置错误码
```

- `Content-Length` 由框架自动补齐（body + 文件大小）。
- 文件响应示例：业务函数里 `open()` 静态文件后 `ResponseFileSet(w, fd, 0)`，
  发送完毕由框架负责 `close(fd)`。
- 响应头/体内存不足时自动降级为 **503**。

## 配置与调优

参数在 `main.c` 的 `WebStater` 与各 `*_init` 中集中设置：

| 参数 | 位置 | 默认 | 说明 |
|---|---|---|---|
| 空闲超时 | `main_t m1 = {.time = 1}` | 1 秒 | 超过无活动返回 408 |
| 在途请求上限 | `ed_store_blocknum` | 100 | 每连接未发完请求数上限（发送槽数） |
| Receiver_Thread 内存池 | `ReceiverThreadInit` → `http_pool(1MB)` / `store_area(2MB)` | — | 读侧：请求副本 / 暂存区 / 发送槽；现为 malloc 过渡实现，参数仅记录配额 |
| Worker_Thread 内存池 | `WorkerThreadInit` → `txt_pool(2MB)` / `http_pool(256KB)` | — | 写侧：响应头/体/解析结构；现为 malloc 过渡实现 |
| 任务队列容量 | `GlobalResourceInit(100)` | 100 | 每个接待线程各一条 Task_Queue（组内工作线程/替补线程共用） |
| 组数 | 环境变量 `CWEB_GROUP_NUM` | 1 | 每组 1 接待线程 + `CWEB_WORKER_NUM` 个工作线程，上限 32；监听用 `SO_REUSEPORT` 分摊连接 |
| 每组工作线程数 | 环境变量 `CWEB_WORKER_NUM` | 2 | 同组 worker 共享该组任务队列，上限 16；实测 2 核上共享队列的 2 worker 反而低于 1 worker，最佳为 `CWEB_GROUP_NUM=2` + `CWEB_WORKER_NUM=1` |

> 当前 `MemoryPool` 是 **malloc/free 过渡实现**：`notfull=0` 只在真正 OOM 时出现，
> 不再有"预分配条数用尽"导致的 503；`*_init` 的参数现在只用于记录配额/统计。

## 内存管理约定

- **所有权明确**：Receiver_Thread 侧内存池（`http_pool`/`store_area`）与 Worker_Thread 侧内存池
  （`txt_pool`/`http_pool`）各自独立，跨线程只转移"使用权"，最终由所属线程释放。
- 响应文本发完后经 `Worker_Queue` 交回 Worker_Thread 释放；请求副本解析完即归还 Receiver_Thread 的 `http_pool`。
- `ResponseBodySet` / `ResponseHeaderSet` 内部自动从 `txt_pool` 分配，业务代码**无需手动释放**
  （打包完成后框架统一回收）。文件 fd 由框架在发送完毕后 `close`。
- 路由表 URL 以**指针保存不复制**：注册时请传静态字符串或确保字符串生命周期覆盖整个服务期。

## 测试

`test/` 目录包含自动化测试（不参与 `make`/`make lib` 编译，`test/src/*.c` 不在通配范围内）：

| 测试 | 覆盖 |
|---|---|
| `test_memory_pool` | 内存池新语义：分配/复用/池满降级/超大 malloc 兜底 |
| `test_rwlock` | 读写锁互斥与阻塞行为 |
| `test_hash` | hash_1/2/3 插入/查找/扩容/删除 |
| `test_Request_Temp_Pool` | 暂存区读入/请求切分/残留保留/扩容 |
| `test_queue` | 三个队列单线程 + 多线程压力 |
| `test_server` | 真实 `WebStater` 集成：200/404/query/8KB/管道化/半包/408/重复头/大请求 |

运行示例：

```bash
# 单元测试（也可以直接 make -C AgentSpace/test run）
gcc -g -O0 AgentSpace/test/src/test_memory_pool.c MemoryPool/MemoryPool.c -I. -o /tmp/t && /tmp/t
# 集成测试（链接全部模块，main.c 用 -Dmain= 避免入口冲突）
# 详见《代码检查与测试报告.md》
```

当前验证结果：**单元用例全过、集成 0 失败、valgrind 0 Invalid/0 SIGSEGV/0 未初始化**。

## 已知限制

- 路由为**静态精确匹配**，不支持 `/user/:id` 动态参数。
- 请求体仅记录指针，不解析表单格式（`x-www-form-urlencoded` 等）。
- 不支持 chunked 传输编码、HTTP/2、WebSocket。
- `MemoryPool` 当前为 **malloc/free 过渡实现**（尚无自研分配器）；`notfull=0` 只在 OOM 时出现。
- 请求副本按 `h_size/1024+1` 向上取整分配，单请求头+体合计建议 < 1MB。

## 版本记录

- 多轮修复（2026-08）：管道化保序、>2KB 请求崩溃、重复请求头崩溃、超时失效、
  内存池扩容悬垂（改非扩容）、定时器陈旧 fd、优先队列越界/计数、send_pointer_pool 503、hash 泄漏。
  详见《代码检查与测试报告.md》。

