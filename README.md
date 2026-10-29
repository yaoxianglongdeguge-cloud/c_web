# c_web（libcweb）—— 纯 C 的 epoll HTTP 服务器库

一个用纯 C 从零实现的 HTTP/1.1 服务器**静态库**。核心是「收发线程 + 业务线程」的
半同步/半异步模型：`epoll` 事件循环负责连接与收发，工作线程负责解析、路由与构造响应，
中间用每组的任务队列解耦；静态文件走 `sendfile` 零拷贝。

> **当前状态**：仅支持 Linux（epoll / eventfd / sendfile / SO_REUSEPORT / POSIX 线程）。
> 本仓库只包含**库本体**，不含示例站点、示例应用和测试程序 —— `main()` 由使用方自己提供，
> 构建产物是 `libcweb.a`。

---

## 目录

- [特性](#特性)
- [架构](#架构)
- [目录结构](#目录结构)
- [构建与快速开始](#构建与快速开始)
- [配置](#配置)
- [对外 API](#对外-api)
- [线程模型与容量](#线程模型与容量)
- [日志与排障](#日志与排障)
- [平台与运行环境](#平台与运行环境)
- [已知限制](#已知限制)
- [变更记录](#变更记录)

---

## 特性

- **半同步/半异步**：每组 `1 个接待线程 + N 个工作线程`。接待线程负责 accept / epoll / 读请求 / 发响应，
  工作线程负责 HTTP 解析 / 路由 / 构造响应。
- **多组监听**：`group.num` 组各自 `socket + SO_REUSEPORT`，由内核按四元组分摊新连接。
- **同连接响应保序**：每个请求分配递增 `serial`，发送端用 `serial % blocknum` 的环形槽位，
  允许乱序处理、保证按请求顺序发出。
- **HTTP 解析**：分块暂存区 + 状态机判定完整性，支持拆包/粘包与同连接流水线（pipelining）；
  URL 与 Query 分离，请求头/查询参数为哈希表。
- **静态文件零拷贝**：`sendfile` + 非阻塞 + `EPOLLOUT` 续写；`TCP_NODELAY` 已默认开启。
- **断连定时器**：小顶堆 + 线性重置，空闲超过 `receiver.idle_timeout_s` 回 408。
- **内存管理**：`MemoryPool` 目前是 `malloc/free` 薄封装（保留原 API），分配失败降级 503；
  响应内存经 `Worker_Queue` 交还工作线程统一释放。
- **配置文件驱动**：`key = value` 文本配置，启动时一次性读入并校验，无需重编译。
- **可观测**：启动摘要 + 分组就绪 + 关键错误日志（见[日志与排障](#日志与排障)）。

---

## 架构

```
WebStater(IP, PORT, Handler)
   │  ① 忽略 SIGPIPE ② 初始化路由表 ③ 回调 Handler 注册路由
   │  ④ 按 group.num 建监听 socket（SO_REUSEPORT）⑤ 每组起接待线程 + 工作线程
   ▼
Receiver_Thread × group.num       （各自独立线程 + 独立 epoll + 独立任务队列）
   │  epoll 事件循环（epoll.tick_ms）
   │  accept / fd 表 / 断连定时器 / 按 serial 有序发送
   ├── Task_Queue（每组私有，容量 task_queue.capacity，双缓冲）
   ▼
Worker_Thread × worker.per_group
   │  解析请求 → RouterHandlerFind(url) → 业务函数写 Response → 打包
   └── Receiver_Queue（投回本组接待线程）
   ▼
Receiver_Thread 发送：按 serial 顺序 write / sendfile
   ▼
Worker_Queue（响应内存归还给工作线程释放）
```

一次请求的生命周期：

```
accept → epoll → 读入请求暂存区 → 状态机判定完整 → 复制到 http_pool
→ Task_Queue → 工作线程解析/路由/构造响应 → Receiver_Queue
→ 接待线程按 serial 保序发送 → 内存经 Worker_Queue 归还
```

---

## 目录结构

| 目录 / 文件 | 职责 |
|---|---|
| `ToOut/foo.h` | **对外 API 头文件**（业务代码只包含它） |
| `Include/` | 内部聚合头 `Include.h`、公共宏 `CWEBCommon.h`、标准头 `IncludeStandard.h`、前置声明 `Variate.h` |
| `Config/CWebConfig.{c,h}` | 运行期配置结构体 + 配置文件解析/校验/打印 |
| `StaterMain/StarterMain.c` | `WebStater` 入口：监听 socket、建组、起线程 |
| `Threads/` | `ReceiverThread`（接待/收发）、`WorkerThread`（业务） |
| `ThreadPool/` | 每组的 worker 池、唤醒与「卡住换线程」 |
| `FdConnectManage/` | `EpollOperate`（读/写事件挂载）、`FdConnectTable`（fd 哈希表）、`Timer`（断连定时器） |
| `FdConnectOrClose/` | accept 新连接、统一关闭与错误响应构造 |
| `FdConnectResource/` | `RequestTempPool`（请求暂存区 + 状态机）、`SendPointerPool`（发送槽） |
| `Queue/` | `TaskQueue`（组内任务）、`ReceiverQueue`（响应队列）、`WorkerQueue`（内存归还）、`TempPackQueue` |
| `RequestCome/` | 读请求、完整性判定、头部/URL/Query 解析、请求头哈希 |
| `ResponseSend/` | 发送主循环（小响应 `write`、文件 `sendfile`） |
| `RequestAndResponse/` | `Request` 只读视图、`Response` 构造接口 |
| `Router/` | URL → Handler 路由（DJB2 + 盐值哈希，精确匹配） |
| `MemoryPool/` | 内存池 API（当前为 malloc/free 实现，配额仅统计） |
| `MyLock/` | 写优先读写锁（预置组件） |
| `Makefile` | 构建静态库 `libcweb.a` |
| `cweb.conf.example` | 配置文件示例 |

---

## 构建与快速开始

### 构建库

```bash
make                              # 默认目标：生成 libcweb.a（-O0，便于调试）
make OPT="-O2 -march=native"      # 发布构建
make lib                          # 等价写法
make clean                        # 清理 .o / .d / libcweb.a
```

> 仓库本身没有 `main()`，`make` 默认只构建静态库。
> 想直接在仓库根生成可执行文件：把含 `main()` 的 `app.c` 放到仓库根，执行 `make main`
> （或按下面的命令手动链接）。

### 写一个最小应用

```c
/* app.c */
#include "ToOut/foo.h"

static void home(Request* r, Response* w)
{
    (void)r;
    ResponseHeaderSet(w, "Content-Type", "text/plain; charset=utf-8");
    ResponseBodySet(w, "hello cweb");
}

static void routes(void)
{
    RouterAppend("/", home);          /* url 请传静态字符串 */
}

int main(void)
{
    /* 成功时 WebStater 不返回；失败返回 -1 */
    return WebStater("127.0.0.1", 5343, routes) == 1 ? 0 : 1;
}
```

```bash
gcc -O2 -Wall -I. app.c -L. -lcweb -lpthread -o app

# 用默认配置（/etc/cweb.conf，不存在则全用内置默认值）
./app

# 或指定配置文件
CWEB_CONF=./my.conf ./app

curl -v http://127.0.0.1:5343/
```

### 静态文件响应

```c
static void logo(Request* r, Response* w)
{
    (void)r;
    const char* path = "WebSite/logo.png";     /* 建议绝对路径，或基于 web.root 拼接 */
    int fd = open(path, O_RDONLY);
    if (fd < 0) { ResponseErrorSet(w, 404); return; }

    ResponseHeaderSet(w, "Content-Type", "image/png");
    ResponseFileSet(w, fd, 0);                 /* Content-Length 自动填，发送完由框架 close(fd) */
}
```

> `web.root` 只提供字符串，框架**不会**自动拼路径；业务函数按自己的规则拼接即可。

---

## 配置

### 文件格式与加载规则

```ini
# 整行注释；key = value，一行一个
listen.port    = 5343
web.root       = /srv/www
worker.parallel = 0
```

- 支持空格/制表符、`\r\n`（Windows 换行）；值里的 `#` 是普通字符（只支持整行注释）。
- **不支持 UTF-8 BOM**：带 BOM 的首行会被当作未知键，启动日志会给出提示。
- 查找顺序：`CWEB_CONF` 指定的文件 → `/etc/cweb.conf`。
  - 显式指定（`CWEB_CONF` 或非 NULL 路径）但文件不存在 → **启动失败**；
  - 默认路径不存在 → 使用内置默认值，正常启动。
- 优先级：**配置文件 > `WebStater(ip, port, ...)` 入参 > 内置默认值**。
  只有文件里没写的键，`WebStater` 的 `ip/port` 才会覆盖。
- **不再读取 `CWEB_xxx` 环境变量来注入配置值**；`CWEB_CONF` 只用于选择配置文件路径。
- 配置只在启动时读一次，**不支持热重载**。改容量类参数必须重启。

完整示例见仓库根 `cweb.conf.example`。

### 配置项一览（括号内为允许范围）

**监听 / 站点**

| 键 | 默认 | 说明 |
|---|---|---|
| `listen.addr` | `127.0.0.1` | 监听地址（不能为空） |
| `listen.port` | `5343` | 监听端口（1..65535） |
| `listen.backlog` | `100` | `listen()` 的 backlog（≥1）；超过内核 `somaxconn` 会被截断，启动日志会提示 |
| `web.root` | `WebSite` | 静态站点根目录字符串（相对路径相对启动时的 CWD） |

**线程模型**

| 键 | 默认 | 说明 |
|---|---|---|
| `group.num` | `1` | 组数（1..32），每组一个监听 socket |
| `worker.per_group` | `4` | 每组工作线程数（2..16） |

总线程数 = `group.num × (1 + worker.per_group) + 1`（`+1` 是 `main` 线程在 `pause()`）。

**接待线程 / 连接**

| 键 | 默认 | 说明 |
|---|---|---|
| `receiver.idle_timeout_s` | `60` | 连接空闲超时秒数（≥1），超时回 408 |
| `receiver.send_batch` | `100` | 每轮事件最多连续发送多少次 |
| `timer.capacity` | `5000` | 定时器节点数（≥1），应 ≥ 单组最大连接数 |
| `fd_table.init_level` | `4` | fd 表初始桶等级（1..5 → 7/17/31/59/113） |

**队列 / 背压**

| 键 | 默认 | 说明 |
|---|---|---|
| `task_queue.capacity` | `100` | 每组任务队列容量（≥1） |
| `temp_pack.capacity` | `1024` | 队列满时的暂存环容量（1..65536），建议 ≥ 任务队列容量 |
| `send.pipeline_window` | `100` | 每连接在途未发完请求上限（发送槽数，≥1） |

**内存池配额（单位 KB，仅统计/告警，不是硬上限）**

| 键 | 默认 | 说明 |
|---|---|---|
| `pool.recv_http_kb` | `1024` | 接待线程：请求副本池（64..65536） |
| `pool.recv_store_kb` | `2048` | 接待线程：请求暂存区（64..65536） |
| `pool.worker_txt_kb` | `2048` | 工作线程：响应头/体（64..65536） |
| `pool.worker_http_kb` | `256` | 工作线程：解析结构（64..65536） |

**HTTP 限额**

| 键 | 默认 | 说明 |
|---|---|---|
| `request.max_url_bytes` | `8192` | 超过回 414（256..65536） |
| `request.max_header_bytes` | `16384` | 超过回 431（1024..65536） |
| `request.max_body_bytes` | `1048576` | 超过回 413（≥1024） |
| `request.max_total_bytes` | `1114112` | 请求总字节上限，必须 ≥ header + body |
| `request.max_header_fields` | `128` | 单个请求最大头字段数（≥8） |
| `request.temp_init_kb` | `2` | 每连接初始暂存区大小（1..64 KB） |
| `request.temp_pool_max_bytes` | `1048576` | 暂存区扩容走 malloc 的阈值（≥1024） |

**工作线程 / 事件循环**

| 键 | 默认 | 说明 |
|---|---|---|
| `worker.stall_ms` | `4` | 主 worker 超过这么久没结束就认为卡住并换线程（1..1000） |
| `worker.sem_wait_ms` | `100` | 任务队列为空时在信号量上的等待时长（1..1000） |
| `worker.idle_sleep_us` | `100000` | 未绑定队列时的休眠微秒数（0..1000000） |
| `worker.parallel` | `0` | `1` = 唤醒同组全部 worker；`0` = 只唤醒 worker[0] |
| `epoll.tick_ms` | `2` | `epoll_wait` 超时（1..1000） |

---

## 对外 API

业务代码只需要 `#include "ToOut/foo.h"`（编译时加 `-I<仓库根>`）。

### 启动

```c
typedef void (*Handler)(Request* r, Response* w);   /* 业务处理函数 */
typedef void (*RouterFunc)();                       /* 注册路由的回调 */

int WebStater(char* IPaddr, int PORT, RouterFunc Handler);

extern int cweb_listen_port;   /* 实际监听端口，可在 /api/info 之类接口里读取 */
```

- `WebStater` 会：忽略 `SIGPIPE` → 读配置 → 初始化路由表 → 调用 `Handler()` 注册路由 →
  按 `group.num` 建监听 socket → 起接待/工作线程 → 当前线程 `pause()` 阻塞。
- **成功不返回**；失败（监听失败、初始化失败）返回 `-1`。
- `IPaddr/PORT` 只是兜底：配置文件里写了 `listen.addr/listen.port` 时以配置为准。

### 路由

```c
int RouterAppend(char* url, Handler func);   /* 成功 1；重复注册/失败 0 */
Handler RouterHandlerFind(char* url);        /* 未命中返回 NULL（框架回 404） */
```

- URL **精确匹配**（不含查询串），不支持动态参数。
- `url` 指针被直接保存、不复制 —— 必须传静态字符串（或保证生命周期覆盖整个服务期）。
- 只能在启动期（`RouterFunc` 回调里）注册。

### 请求（只读）

```c
const char* RequestGetter(Request* r, char* get1, char* get2);
```

| `get1` | `get2` | 返回 |
|---|---|---|
| `"Method"` / `"Url"` / `"Version"` / `"Body"` | `NULL` | 对应字符串；`Url` 已剥离查询串 |
| `"Headers"` / `"Query"` | 键名 | 该头/该查询参数的值 |

未命中返回 `NULL`（不同于空串）。`Query` 是**未解码**的原始串（`%XX`、`+` 不还原）。

### 响应

```c
int ResponseBodySet(Response* r, char* c);                   /* 文本响应体 */
int ResponseHeaderSet(Response* r, char* key, char* value);  /* 追加自定义头 */
int ResponseFileSet(Response* r, int fd, off_t offset);      /* 文件响应（sendfile） */
int ResponseErrorSet(Response* r, int error);                /* 错误码 */
```

- `Content-Length` 由框架自动补齐（body 或文件大小）；`Body` 与 `File` 二选一。
- `ResponseFileSet` 的 `fd` 由框架在发送完成后 `close`，业务不要重复关闭。
- 响应头按调用顺序**直接追加，不做去重**；单个响应的头区固定 4KB，写不下时返回 0 并置 503。
- 内存不足时自动降级 **503**（`ResponseBodySet`/`ResponseHeaderSet` 返回 0，`error_reason=503`）。
- 支持的状态码：`400 404 405 408 411 413 414 431 500 503`（`ResponseErrorSet` 会生成对应文本响应）。

---

## 线程模型与容量

- **每组**：1 个接待线程（独立 `epoll`、独立监听 fd、独立任务队列、独立 fd 表/定时器）
  + `worker.per_group` 个工作线程。
- **保序**：同连接请求按 `serial` 递增，发送端用 `serial % send.pipeline_window` 的槽位，
  保证响应顺序 == 请求顺序（处理可以乱序）。
- **换线程**：接待线程每轮检查主 worker 的 `taskStart/taskEnd`，超过 `worker.stall_ms`
  未结束就读取该线程在 `/proc/self/task/<tid>/stat` 的状态，`S/D` 时切换到空闲备用 worker。
  `/proc` 不可用时该机制会自动失效，并在日志里告警。

容量关系建议（配置校验只查单值范围，跨键约束要自己保证）：

- `timer.capacity ≥ 单组最大并发连接数`，否则连接挂不上定时器（永不超时）。
- `temp_pack.capacity ≥ task_queue.capacity`，否则队列一满就直接 503。
- `request.max_total_bytes ≥ request.max_header_bytes + request.max_body_bytes`（已有校验）。
- `listen.backlog ≤ /proc/sys/net/core/somaxconn`，否则被内核静默截断。
- `pool.recv_store_kb` 与 `send.pipeline_window × request.temp_init_kb × 并发连接数` 相关，
  过小会频繁走 malloc 扩容（`request.temp_pool_max_bytes` 是扩容阈值）。

---

## 日志与排障

> 完整的日志清单（文件 / 行号 / 作用 / 限流次数）见仓库根 [`LOG_REPORT.md`](LOG_REPORT.md)。

**统一规则**：

- **信息** → `stdout`：启动摘要、配置生效值、组/线程就绪。启动期在 `CWebConfig.c`、`StarterMain.c`、
  `ReceiverThread.c`、`WorkerThread.c` 各有一处 `fflush(stdout)`，重定向到文件也能立刻看到。
- **告警/错误** → `stderr`（无缓冲，进程被 kill 也不会丢）。
- 格式统一为 `[标签] 说明：详情`，每条以 `\n` 结尾。
- 同类高频日志用 `static` 计数限流（1 / 3 / 5 / 10 / 20 / 50 次），避免故障时刷屏。
- `[Config]` 的逐行明细最多 20 条，其余在末尾汇总一行。
- 调试日志受 `-DCWEB_DEBUG_SWITCH` 控制，发布构建不输出。

| 前缀 | 来源 | 内容 | 限流 |
|---|---|---|---|
| `[Config]` | `Config/CWebConfig.c` | 配置文件来源、未知键/值非法/重复键、校验失败、BOM 提示、生效配置 | 明细 20 + 汇总 |
| `[WebStater]` | `StaterMain/StarterMain.c` | 启动摘要、某组监听/建线程失败、组数未完全生效 | 启动期一次 |
| `[Net]` | `StarterMain` / `FdConnectOrClose` | `socket/setsockopt/bind/listen/accept/TCP_NODELAY` 失败、backlog 截断、fd 用尽 | accept / NODELAY 各 3 |
| `[Recv]` | `Threads/ReceiverThread.c` | `epoll_create1/eventfd/线程池` 初始化失败、组就绪、`epoll_wait` 失败 | `epoll_wait` 3 |
| `[Worker]` | `Threads/WorkerThread.c` | 线程就绪（id+tid）、`sem_timedwait` 失败、未绑定任务队列 | 3 / 1 |
| `[Pool]` | `ThreadPool/ThreadPool.c` | `/proc` 不可用、`malloc/pthread_create` 失败、卡住换线程（含 `CWEB_DEBUG_SWITCH` 调试） | 1 / 10 / 50 |
| `[Epoll]` | `FdConnectManage/EpollOperate` | `epoll_ctl` / `fcntl(O_NONBLOCK)` 失败 | 5 |
| `[Fd]` | `FdConnectOrClose.c` | 非 200 关闭连接的原因（408/413/414/431/503） | 10 |
| `[Mem]` | `MemoryPool/MemoryPool.c` | 初始化参数非法 / `calloc` 失败 / `malloc` 失败 | 5 |
| `[Router]` | `Router/Router.c` | 路由表初始化失败、注册失败（重复 URL） | 20 |
| `[RouterHash]` | `Router/RouterDataStruct/RouterHash.c` | 路由表达扩容上限，停止扩容 | 1 |
| `[Send]` | `ResponseSend/ResponseSendMain.c` | `write/sendfile` 失败（正常断连不打印） | 3 |

典型输出：

```
[Config] 已加载配置文件：/etc/cweb.conf
[Config] 生效配置：
  listen.addr=127.0.0.1  listen.port=5343  ...
[WebStater] 组数=1，每组接待线程=1，每组工作线程=4，线程总数≈6
[Recv] 组1 就绪：epfd=4 wakeup_fd=5 worker=4
[Worker] 线程就绪：id=0 tid=89088
```

排查建议：

- 端口起不来：看 `[Net] bind(ip:port) 失败` 的 `errno`，以及 `[WebStater] 只有 x/N 组启动成功`。
- 配置没生效：看 `[Config] 未知键 …` / `提示：没有识别到任何已知键`（多为 BOM 或拼错）。
- 压测上不去：确认 `[Net] listen.backlog … 超过内核 somaxconn …`，再调 `worker.per_group`。
- 连接被服务端关掉：看 `[Fd] 关闭连接 … 原因=408/413/414/431/503` 定位是哪条限额触发。
- 卡死连接不超时：`timer.capacity` 是否小于并发连接数。

---

## 平台与运行环境

- **仅 Linux**：使用 `epoll_create1`/`epoll_wait`、`eventfd`、`sendfile`、`SO_REUSEPORT`、
  `pthread`、POSIX 信号量、`SYS_gettid`、`/proc`、`__atomic_*`。
- 建议 **内核 ≥ 3.9**（`SO_REUSEPORT`）。老内核上多组会因 `bind` 失败而退化成单组，
  启动日志会明确提示。
- 建议 **64 位**（`off_t`/`time_t`）。32 位需加 `-D_FILE_OFFSET_BITS=64`（glibc ≥2.34 再配 `-D_TIME_BITS=64`），
  否则 `sendfile` 无法处理 >2GB 文件，定时器有 2038 问题。
- 运行限制：
  - `ulimit -n`：每连接 1 个 fd，过小会 `accept` 失败（日志提示 `fd 用尽`）；
  - cgroup `pids.max`：总线程数 = `group.num × (1 + worker.per_group) + 1`；
  - `net.core.somaxconn`：`listen.backlog` 的真实上限；
  - `/proc` 可读：否则 `worker.stall_ms` 换线程失效；
  - `MemoryPool` 是 malloc 实现，`pool.*_kb` 不是硬上限，容器内存限额下仍可能被 OOM kill。

---

## 已知限制

- 仅 HTTP/1.1；不支持 TLS、HTTP/2、WebSocket。
- 不支持 `Transfer-Encoding: chunked`；带体请求需要 `Content-Length`（否则 411）。
- 不处理 `Connection: close`：连接默认保持，直到空闲超时或出错。
- 路由为静态精确匹配，不支持 `/user/:id` 这类动态参数。
- 请求体只提供指针，不解析表单（`x-www-form-urlencoded`/multipart 需业务自己处理）。
- 配置只有 `key = value` 一种语法，无 include/分段覆盖机制，不支持热重载。
- `MemoryPool` 尚未换成自研分配器，配额只用于统计/告警。
- 无优雅退出：进程收到的终止信号按系统默认行为处理。

