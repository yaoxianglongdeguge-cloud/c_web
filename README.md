# c_web —— 纯 C 实现的并发 HTTP 服务器框架

一个用纯 C 从零实现的并发 HTTP/1.1 服务器，以**静态库**形式提供给上层框架使用。
采用"收发线程 + 业务线程"的半同步/半异步模型，内置内存池、无锁化连接表、
HTTP 状态机解析、有序发送、断连定时器与跨线程内存所有权管理。

## 核心特性

- **半同步/半异步线程模型**：`worker`（收发线程，负责 accept / epoll / 读请求 / 发响应）
  + `profession`（业务线程，负责 HTTP 解析 / 路由分发 / 构造响应），通过全局任务队列解耦。
- **请求保序**：每个请求从读入起分配递增序号 `serial`，发送端用环形槽位
  `serial % blocknum` 保证同连接上响应顺序与请求顺序一致（乱序处理、有序发出）。
- **HTTP 状态机 + 分块暂存**：`http_ed_store` 处理拆包/粘包；`http_state` 判定请求完整性；
  请求超过暂存区大小时自动扩容。
- **内存池**：按 2 的幂分级，非扩容分配（池满返回 `notfull=0`，由调用方降级为 503）；
  跨线程内存通过 `Memory_Queue` 归还，绝不跨线程 free。
- **大文件零拷贝**：文件响应使用 `sendfile`，内存中响应支持 1MB 以内。
- **断连定时器**：小顶堆 + 线性扫描重置，空闲连接超时返回 408（默认 1 秒）。
- **路由表**：DJB2+盐值哈希，URL 精确匹配，支持启动时静态注册。
- **错误响应**：400/404/405/408/411/413/414/431/500/503 自动生成文本响应。

## 架构概览

```
        ┌──────────────────────────── Web_Driver(IP, PORT, Handler) ───────────────────────────┐
        │  ① 忽略 SIGPIPE → ② 初始化全局路由表/任务队列 → ③ 回调 Handler() 注册路由              │
        │  ④ 创建监听 socket → ⑤ 启动线程                                                    │
        └──────────────────────────────┬───────────────────────────────────────────────────────┘
                                       │
                ┌──────────────────────┴───────────────────────┐
                │  worker 线程（当前直接跑在 Web_Driver 调用线程）│
                │  · epoll 事件循环（50ms 超时）                │
                │  · accept / fd 表 / 定时器 / 有序发送         │
                └──────────────────────┬───────────────────────┘
                                       │ Task_Queue（全局，容量100，双信号量+锁）
                ┌──────────────────────▼───────────────────────┐
                │  profession 线程 ×2（业务处理）                │
                │  · HTTP 解析 / 路由分发 / 构造响应            │
                │  · 结果投回对应 worker 的发送队列             │
                └──────────────────────┬───────────────────────┘
                                       │ Send_thing_queue（每 worker 一个）
                ┌──────────────────────▼───────────────────────┐
                │  worker 发送端：按 serial 保序 write / sendfile│
                └──────────────────────┬───────────────────────┘
                                       │ Memory_Queue（响应内存归还给 profession 释放）
                                       ▼
```

一次请求的生命周期：

```
accept → epoll → http_main 读入暂存区 → http_state 判定完整请求 → 复制到 http_pool
→ 投递 Task_Queue → profession: 解析头部/URL/Query → Handler_Find(url) → 业务函数写 Response
→ pack_task 打包 → Send_thing_queue → worker 按 serial 保序发送 → 内存经 Memory_Queue 归还
```

## 目录结构

| 目录/文件 | 职责 |
|---|---|
| `main.c` / `main.h` | `Web_Driver()` 启动驱动（框架入口） |
| `server/` | 全局资源：路由表 + 任务队列初始化 |
| `my_thread/` | `worker_thread`（收发线程）、`profession_thread`（业务线程） |
| `connect_fd/` | 连接 fd 哈希表（fd → 暂存区/发送槽/序号） |
| `connect_config/` | 连接管理：accept / 统一关闭 `fd_close` |
| `http_analysis/` | HTTP 读入、暂存区、状态机、头部/URL/Query 解析 |
| `send_tool/` | 有序发送槽环形缓冲 + 发送主逻辑 |
| `queue/` | `task_queue`（全局任务）、`send_thing_queue`（发送事件）、`memory_queue`（内存回收） |
| `memory_pool/` | 分级内存池（非扩容，notfull 降级） |
| `timer/` | 断连计时器（小顶堆 + `timer_free` 移除） |
| `data_struct/` | hash_1 路由表 / hash_2 请求头键值 / hash_3 fd 表 / prior_queue_1 小顶堆 |
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

> 注意：本仓库 `main.c` 只提供 `Web_Driver()` 启动驱动，**不含 `main()` 入口**，
> 由上层框架提供程序入口（服务器实例）。Makefile 的 `main` 目标仅为本地调试预留。

## 快速上手

```c
#include "include.h"

/* 业务处理函数：收到匹配请求时被调用 */
static void handler_home(Request* r, Response* w)
{
    (void)r;
    Response_body_set(w, "hello cweb");                    /* 写响应体 */
    Response_header_set(w, "Content-Type", "text/plain");  /* 自定义响应头 */
}

static void handler_echo(Request* r, Response* w)
{
    const char* name = Request_get(r, "Query", "name");    /* 取查询参数 */
    char buf[128];
    snprintf(buf, sizeof(buf), "echo:%s", name ? name : "(null)");
    Response_body_set(w, buf);
}

/* 路由注册回调：在监听 socket 建立前被 Web_Driver 调用 */
static void register_routes(void)
{
    Handler_append("/", handler_home);
    Handler_append("/echo", handler_echo);
}

int main(void)
{
    /* 阻塞运行：Web_Driver 会直接在当前线程跑 worker 事件循环 */
    Web_Driver("127.0.0.1", 8899, register_routes);
    return 0;
}
```

## API 参考

### 启动

```c
int Web_Driver(char* IPaddr, int PORT, HandlerFunc Handler);
```

- `Handler`：`void (*)(void)` 回调，在 socket 建立**之前**执行，用于 `Handler_append` 注册路由。
- 内部流程：忽略 SIGPIPE → 初始化全局路由表与任务队列 → 回调注册路由 →
  创建监听 socket → 启动 2 个 profession 线程 → **当前线程进入 worker 事件循环（阻塞）**。

### 路由

```c
typedef void (*Handler)(Request*, Response*);
int Handler_append(char* url, Handler func);   // 注册 URL → 处理函数（静态精确匹配）
Handler Handler_Find(char* url);               // 查找（未命中返回 NULL → 404）
```

### 请求（只读）

```c
typedef struct Request {
    const char* const Method;      // "GET" / "POST" ...
    const char* const Url;         // 去掉查询串的路径，如 "/echo"
    const char* const Version;     // "HTTP/1.1"
    const Hash_map_2* const Query; // 查询参数键值表
    const Hash_map_2* const Headers;// 请求头键值表
    const char* const Body;        // 请求体
} Request;

const char* const Request_get(Request* r, "Method"|"Url"|"Version"|"Body", NULL);
const char* const Request_get(Request* r, "Headers"|"Query", "键名");  // 查头/查参数
```

### 响应

```c
int Response_body_set(Response* r, char* c);      // 设置文本响应体（自动分配内存）
int Response_header_set(Response* r, char* key, char* value);  // 追加自定义头
int Response_fd_set(Response* r, int fd, off_t offset);        // 文件响应（sendfile 零拷贝）
int Response_error_set(Response* r, int error);                 // 设置错误码
```

- `Content-Length` 由框架自动补齐（body + 文件大小）。
- 文件响应示例：业务函数里 `open()` 静态文件后 `Response_fd_set(w, fd, 0)`，
  发送完毕由框架负责 `close(fd)`。
- 响应头/体内存不足时自动降级为 **503**。

## 配置与调优

参数在 `main.c` 的 `Web_Driver` 与各 `*_init` 中集中设置：

| 参数 | 位置 | 默认 | 说明 |
|---|---|---|---|
| 空闲超时 | `main_t m1 = {.time = 1}` | 1 秒 | 超过无活动返回 408 |
| 在途请求上限 | `ed_store_blocknum` | 100 | 每连接未发完请求数上限（发送槽数） |
| worker 内存池 | `worker_init` → `http_pool(6,6,500,400)` / `store_area(10,6,500,400)` | — | 读侧：请求副本 / 暂存区 / 发送槽 |
| profession 内存池 | `profession_init` → `txt_pool(7,7,200,120)` / `http_pool(6,6,10,10)` | — | 写侧：响应头/体/解析结构 |
| 任务队列容量 | `global_resource_init(100)` | 100 | 全局 Task_Queue |
| profession 线程数 | `Web_Driver` 内 `pthread_create(func2)` | 2 | 可增减 |

> 内存池为**非扩容**语义：某大小类并发占用超过预分配条数时，新分配返回 `notfull=0`，
> 上层降级 503。压测规模大时请调大 `*_init` 的 `strip_num` 参数。

## 内存管理约定

- **所有权明确**：worker 侧内存池（`http_pool`/`store_area`）与 profession 侧内存池
  （`txt_pool`/`http_pool`）各自独立，跨线程只转移"使用权"，最终由所属线程释放。
- 响应文本发完后经 `Memory_Queue` 交回 profession 释放；请求副本解析完即归还 worker 的 `http_pool`。
- `Response_body_set` / `Response_header_set` 内部自动从 `txt_pool` 分配，业务代码**无需手动释放**
  （打包完成后框架统一回收）。文件 fd 由框架在发送完毕后 `close`。
- 路由表 URL 以**指针保存不复制**：注册时请传静态字符串或确保字符串生命周期覆盖整个服务期。

## 测试

`test/` 目录包含自动化测试（不参与 `make`/`make lib` 编译，`test/src/*.c` 不在通配范围内）：

| 测试 | 覆盖 |
|---|---|
| `test_memory_pool` | 内存池新语义：分配/复用/池满降级/超大 malloc 兜底 |
| `test_rwlock` | 读写锁互斥与阻塞行为 |
| `test_hash` | hash_1/2/3 插入/查找/扩容/删除 |
| `test_http_ed_store` | 暂存区读入/请求切分/残留保留/扩容 |
| `test_queue` | 三个队列单线程 + 多线程压力 |
| `test_server` | 真实 `Web_Driver` 集成：200/404/query/8KB/管道化/半包/408/重复头/大请求 |

运行示例：

```bash
# 单元测试
gcc -g -O0 test/src/test_memory_pool.c memory_pool/memory_pool.c -o /tmp/t && /tmp/t
# 集成测试（链接全部模块，main.c 用 -Dmain= 避免入口冲突）
# 详见《代码检查与测试报告.md》
```

当前验证结果：**单元 5/5 全过、集成 0 失败、valgrind 0 Invalid/0 SIGSEGV/0 未初始化**。

## 已知限制

- 路由为**静态精确匹配**，不支持 `/user/:id` 动态参数。
- 请求体仅记录指针，不解析表单格式（`x-www-form-urlencoded` 等）。
- 不支持 chunked 传输编码、HTTP/2、WebSocket。
- 内存池非扩容：超过预分配条数时降级 503（可调大 `*_init` 参数）。
- 请求副本按 `h_size/1024+1` 向上取整分配，单请求头+体合计建议 < 1MB。

## 版本记录

- 多轮修复（2026-08）：管道化保序、>2KB 请求崩溃、重复请求头崩溃、超时失效、
  内存池扩容悬垂（改非扩容）、定时器陈旧 fd、优先队列越界/计数、send_tool 503、hash 泄漏。
  详见《代码检查与测试报告.md》。

