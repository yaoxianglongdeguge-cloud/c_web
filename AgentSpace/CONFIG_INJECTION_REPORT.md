# c_web 配置注入检查报告（宏 → 运行期变量）

- 检查日期：2026-10-27
- 检查范围：与 `AgentSpace/CONSTANT_MACRO_REPORT.md` 完全相同的源码范围
- 本次只读检查，**没有修改任何源码**；本文件是"把哪些量从硬编码/宏升级成运行期配置"的清单与方案
- 行号以当前工作区为准；开始改之后请按"符号 + 描述"定位，不要只认行号

---

## 0. 这份报告和 `CONSTANT_MACRO_REPORT.md` 的分工

| | `CONSTANT_MACRO_REPORT.md`（已存在） | 本报告 |
|---|---|---|
| 关注点 | 裸数字 → **编译期宏** | 宏/裸数字 → **运行期变量 + 配置文件注入** |
| 产物 | `Include/CWEBConfig.h`（一堆 `#define`） | `Config/CWebConfig.c/h`（一个结构体 + 解析器） |
| 改一次要 | 重编译 | 改配置文件 + 重启 |
| 适合 | 结构布局、协议格式、算法常量 | 部署环境相关：端口、目录、线程数、容量、超时、限额 |

**两件事不冲突，是叠加关系**：

```
                     ┌──────────────────────────────┐
裸数字  ──宏化──▶    │ CWEBConfig.h  里的 #define    │  ← 宏 = 编译期默认值
                     └──────────────┬───────────────┘
                                    │  运行期覆盖（可选）
                                    ▼
                     ┌──────────────────────────────┐
                     │ cweb.conf / 环境变量 → g_cfg  │  ← 配置 = 部署期覆写
                     └──────────────────────────────┘
```

也就是说：**宏继续保留，作为"默认值"和"-D 覆盖"的入口；配置文件只负责在启动时覆盖其中一部分。**
这样做的两个好处：

1. 不配置时行为与现在**完全一致**（默认值就是现在的宏值），便于回滚和 A/B；
2. 想彻底去掉配置文件，`CWebConfigLoad()` 直接返回"全默认"即可，其它代码不用动。

> 本报告出现的编号 `A-01 / C-09 / D-03 / E-04 / G-01` 都指向 `CONSTANT_MACRO_REPORT.md` 里的条目，方便两边的清单对照着改。

---

## 1. 判定标准：留宏还是变变量

一个量要变成运行期变量，必须同时满足 ①②；只要踩到 ③ 就必须留宏。

**① 与"部署环境"有关**：换机器、换域名、换流量就要改（端口、目录、线程数、容量、超时、限额）。
**② 只在启动期读取一次**：读进来拷到各线程自己的局部量，运行期不再变化（真正的热更新要走信号/重载，见 §9-7）。
**③ 影响 `sizeof` / 内存布局 / 协议格式**：例如 `TEMP_PACK_MAX` 是 `Temp_Pack.e[]` 的数组长度，它在结构体里；这类要么保持宏，要么**先把结构改成"指针 + 容量"**才能配置。

再加两条实践约束：

- **单位必须写在键名里**（`_s` / `_ms` / `_us` / `_kb` / `_bytes`）——现在 `ONE_TASK_TIME 4` 就是反面教材（是毫秒，但名字完全看不出来）。
- **每个键都要有合法区间和越界策略**（拒绝启动 > 静默 clamp）。容量类必须拒绝启动，否则线上才发现挂不上计时器。

---

## 2. 结论摘要

1. **能配置的量共 31 个**：**P0 17 个**（不改就没法部署），**P1 14 个**（调优会动）。
2. **必须继续留宏的 7 类**：队列块内布局（`*_QUEUE_ALLOC_BYTES`）、`Temp_Pack` 数组长度、哈希桶阶梯、HTTP 协议常量、方法编号/状态文本、解析块布局（`ANALYSISER_HASH_BUCKETS`）、响应首行长度（见 §3）。
3. **配置化之前必须先解决 5 个现存问题**（见 §9-1 ~ §9-5），否则"注入了也不生效"或"注入后直接崩"：
   - `CWEB_WORKER_NUM` 根本没接上（实际固定 4）
   - `/api/info` 端口写死 5343（换端口就报错数据）
   - `FdTableInit(&t,4)` 传的是"阶梯下标"，越界会拿 0 当桶数
   - `extern int taskQueueBlocknum` 是死声明
   - `MAX_BODY_SIZE`(1MB) 与 `MAX_REQUEST_BYTES`(1088KB) 两套阈值，需要合并成一组
4. **池配额现在只是统计**（MemoryPool 已是 malloc 薄封装），配置化后它是"告警阈值/未来硬上限"，不会真的拦住内存增长。
5. 建议新增 `Config/CWebConfig.{c,h}`，加载顺序：**内置默认 → 配置文件 → 环境变量**，启动日志打印全部生效值。
6. 报告里所有"默认值"都等于**当前代码里的值**，所以引入配置后不写配置文件 = 行为完全不变，可以随时一键回滚。

---

## 3. 必须保留为宏的量（不能做运行期变量）

| 位置 | 符号 | 为什么必须留宏 |
|---|---|---|
| `Queue/ReceiverQueue/ReceiverQueue.h:4-8`<br>`Queue/WorkerQueue/WorkerQueue.h:7-11` | `RECEIVER_QUEUE_ENTRY_BYTES`、`_NODE_OFFSET`、`_ALLOC_BYTES`、`WORKER_QUEUE_*` | 决定"节点搭便车放在响应块尾部"的块内布局；用到了 `sizeof(struct)`，本质是编译期常量 |
| `Threads/ReceiverThread.h:4,19` | `TEMP_PACK_MAX` | 是 `Temp_Pack_Entry e[TEMP_PACK_MAX]` 的数组长度，**直接进结构体**。实测 `sizeof(Temp_Pack)=32784`，即**每组常驻 32KB**。想配置必须先改结构（§9-1） |
| `RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.h:20` | `ANALYSISER_HASH_BUCKETS 7` | 参与 `AnalysiserHashMemOverhead()` 的解析块大小计算，改了要重编 |
| `AnalysiserHash.h:21` | `ANALYSISER_HASH_SPILL_MAX 128` | 同上（参与 `sizeof` 计算）；但它同时是"头字段过多 → 400"的策略阈值，**建议拆成两个**：宏负责布局、配置负责策略（§4 P1） |
| `Router/RouterDataStruct/RouterHash.c` 的质数阶梯、`FdConnectTable.c:4-11` 的 `PRIME_BUCKET_SIZES_3[]` | 桶数递增序列 | 算法常量；配置只暴露"初始级别/目标桶数"，阶梯本身留宏 |
| `Threads/WorkerThread.c:6` | `RSP_FIRST_LINE 17` | 就是 `"HTTP/1.1 200 OK\r\n"` 的长度，建议写成 `(int)(sizeof("HTTP/1.1 200 OK\r\n")-1)` 自校验 |
| `FdConnectOrClose.c:70-85`、`RequestIntergrity.c:5`、`StateMachine.h` 的方法编号 | 状态码/文本/方法表 | 协议常量，配了只会制造事故 |
| `IncludeStandard.h` 里的 include 集合 | — | 编译期 |

---

## 4. 应该由配置文件注入的量

命名约定：配置文件里用 `节.键`（点号分层）；环境变量用 `CWEB_节_键`（全大写下划线），例如
`listen.port` ←→ `CWEB_LISTEN_PORT`。**现有的 `CWEB_GROUP_NUM` / `CWEB_WORKER_NUM` / `CWEB_PARALLEL` 保持可用**（作为兼容别名）。

单位统一写进键名：`_s` 秒 / `_ms` 毫秒 / `_us` 微秒 / `_kb` KB / `_bytes` 字节 / `_num` 个数。

### 4.1 P0：部署必改（17 个）

| # | 键名 | 位置（现值） | 默认 | 单位 | 合法区间 | 关联项 |
|---|---|---|---|---|---|---|
| 1 | `listen.addr` | `AgentSpace/main.c:9` `"127.0.0.1"` | 127.0.0.1 | 点分 IP | 能 `inet_addr` 解析 | G-01 |
| 2 | `listen.port` | `AgentSpace/main.c:9` `5343` | 5343 | 端口 | 1–65535 且 >1024 | G-01 |
| 3 | `listen.backlog` | `StaterMain/StarterMain.c:75` `listen(fd,100)` | 100 | 个数 | 1–`somaxconn` | C-11 |
| 4 | `web.root` | `AgentSpace/WebApp/WebApp.c:26` `"WebSite"` | WebSite | 路径 | 启动时必须存在且是目录 | G-02 |
| 5 | `group.num` | `StarterMain.c:117` env `CWEB_GROUP_NUM` | 1 | 组 | 1–32 | C-04 |
| 6 | `worker.per_group` | `StarterMain.c:118` 算出 `workers`，`:136` 却传 `PER_RECEIVER_WORKER` | 4 | 线程 | 1–16；`ThreadPoolInit` 要 >1 才建池 | A-01 / C-04 |
| 7 | `timer.capacity` | `Threads/ReceiverThread.c:69` `TimerInit(...,5000)` | 5000 | 节点数 | **≥ 单组最大连接数**，建议 ≥2× | C-09 |
| 8 | `fd_table.init_level` | `ReceiverThread.c:70` `FdTableInit(...,4)` | 4 | 阶梯下标 | 1–5（数组只有 5 项有值） | C-10 / F-06 / A-05 |
| 9 | `receiver.idle_timeout_s` | `StarterMain.c:141` `m1->time=60` | 60 | 秒 | 1–3600 | C-12 |
| 10 | `pool.recv_http_kb` | `ReceiverThread.c:60` `1024*1024` | 1024 | KB | 64–65536 | D-01 |
| 11 | `pool.recv_store_kb` | `ReceiverThread.c:62` `2048*1024` | 2048 | KB | 64–65536 | D-02 |
| 12 | `pool.worker_txt_kb` | `Threads/WorkerThread.c:103` `2048*1024` | 2048 | KB | 64–65536 | D-03 |
| 13 | `pool.worker_http_kb` | `WorkerThread.c:107` `256*1024` | 256 | KB | 64–65536 | D-04 |
| 14 | `request.max_total_bytes` | `RequestCome/RequestComeMain/RequestComeMain.c:4` `1088KB` | 1114112 | 字节 | ≥ `max_header + max_body` | E-01 / I-06 |
| 15 | `request.max_url_bytes` | `RequestCome/RequestAnalysis/RequestAnalysiser.c:3` `8192` | 8192 | 字节 | 256–65536（414 阈值） | E-02 |
| 16 | `request.max_header_bytes` | `RequestAnalysiser.c:4` `16384` | 16384 | 字节 | ≥ 1024（431 阈值） | E-03 |
| 17 | `request.max_body_bytes` | `RequestCome/RequestIntergrity/RequestIntergrity.c:4` `1MB` | 1048576 | 字节 | ≤ `max_total`（413 阈值） | E-04 |

**第 7 项要点**：`TimerInit(capacity)` 是计时器队列容量；连接进来在 `FdConnectOrClose.c:28` 无条件 `TimerAlloc`，容量不够会**挂不上计时器**（表现为该连接永不超时、fd 泄漏）。所以它是 P0，并且启动时必须校验 `timer.capacity ≥ group.num × 单组最大连接数`。

**第 6 项要点**：A-01 修完后 `worker.per_group` 才真正生效；现在改它没用。

**第 10–13 项要点**：MemoryPool 现在是 malloc 薄封装，**这四个值目前只用于 `MemoryPoolStat` 统计**，不会限制内存。配置化后可拿它做"用量超过配额 80% 打 warn"。

### 4.2 P1：压测/调优会动（14 个）

| # | 键名 | 位置（现值） | 默认 | 单位 | 合法区间 | 关联项 |
|---|---|---|---|---|---|---|
| 18 | `task_queue.capacity` | `GlobalResource/GlobalResource.h:3` `TASK_QUEUE_NUM 100` → `ReceiverThread.c:67` | 100 | 条 | 16–65536；`Task_Queue_Entry=40B`，双缓冲 100 → 8KB/组 | C-01 |
| 19 | `temp_pack.capacity` | `Threads/ReceiverThread.h:4` `TEMP_PACK_MAX 1024` | 1024 | 条 | 需先改结构（§9-1）；32B/条 → 1024 就是 32KB/组 | C-02 |
| 20 | `receiver.epoll_events` | `ReceiverThread.c:117,122` `events[1024]` / `epoll_wait(...,1024,...)` | 1024 | 个 | 64–4096；12B/个 → 1024 约 12KB **栈** | C-05 |
| 21 | `receiver.send_batch` | `ReceiverThread.c:137` `for(i<100)` | 100 | 次/轮 | 1–1024 | C-06 |
| 22 | `send.pipeline_window` | `StarterMain.c:142` `ed_store_blocknum=100` | 100 | 块/连接 | 1–4096；与 `pool.recv_store_kb` 联动 | C-13 |
| 23 | `worker.stall_ms` | `GlobalResource.h:5` `ONE_TASK_TIME 4` | 4 | 毫秒 | 1–1000 | C-03 |
| 24 | `worker.sem_wait_ms` | `Threads/WorkerThread.c:150` `+100000000ns` | 100 | 毫秒 | 1–10000 | C-07 |
| 25 | `worker.idle_sleep_us` | `WorkerThread.c:143` `usleep(100000)` | 100000 | 微秒 | 0–1000000 | C-08 |
| 26 | `request.temp_init_kb` | `RequestComeMain.c:29` `RequestTempPoolAlloc(...,2)` | 2 | KB | 1–64 | D-05 |
| 27 | `request.temp_pool_max_bytes` | `FdConnectResource/RequestTempPool/RequestTempPool.c:4` `1MB` | 1048576 | 字节 | ≤ `pool.recv_store_kb*1024/2` | D-06 |
| 28 | `response.header_max_bytes` | `WorkerThread.c:5` `RSP_HEADER_BLOCK (4*1024)` | 4096 | 字节 | 512–65536 | D-07 / A-03 |
| 29 | `request.max_header_fields` | `AnalysiserHash.h:21` `SPILL_MAX 128`（策略面） | 128 | 个 | 8–4096 | F-05 |
| 30 | `epoll.tick_ms` | `GlobalResource.h:14` `CWEB_EPOLL_TICK_MS 2`（已有 `#ifndef`） | 2 | 毫秒 | 1–100 | C-05 |
| 31 | `error.buf_bytes` | `FdConnectOrClose/FdConnectOrClose.c:44` `char re[1024]` | 1024 | 字节 | ≥512 | D-08 |

> 第 28 项注意 A-03：`RSP_HEADER_BLOCK` 现在是 4KB，但 `WorkerThread.c:4` 注释写的是 16KB；且 `PackTotal()` 用 `RSP_FIRST_LINE` 推算总长度，改这个值要连带看。
> 第 19 项配置化前**必须先把 `Temp_Pack.e[]` 改成指针 + 容量**（§9-1）。
> 第 29 项：布局用的 `ANALYSISER_HASH_SPILL_MAX` 留宏，另建配置项控制"超过就 400"，两者默认值相同。


---

## 5. 配置项之间的耦合与启动校验

这些数不是独立的，改一个不管另一个就会在线上出怪现象。**建议在 `CWebConfigLoad()` 末尾做一次集中校验，任何一条不满足就打印原因并 `exit(1)`**（拒绝启动比带病启动好排查）。

| 编号 | 规则 | 不满足的后果 | 处理 |
|---|---|---|---|
| V-01 | `timer.capacity ≥ group.num × 单组最大连接数` | 连接挂不上计时器 → 永不超时、fd 泄漏 | 拒绝启动 |
| V-02 | `fd_table.init_level ∈ [1,5]` | `PRIME_BUCKET_SIZES_3` 越界取到 0 → `FdTableAllocate(h,0)` | 拒绝启动 |
| V-03 | `request.max_total_bytes ≥ request.max_header_bytes + request.max_body_bytes` | 大请求永远走不到 413，先被"总长度"掐断，状态码与文档不一致 | 拒绝启动 |
| V-04 | `request.max_body_bytes ≤ request.max_total_bytes` | 同上 | 拒绝启动 |
| V-05 | `pool.worker_txt_kb × 1024 ≥ task_queue.capacity × response.header_max_bytes × 1.2` | 未来换回真池时响应头池会先满 → 503 | warn（现在池不硬限） |
| V-06 | `pool.recv_store_kb ≥ (send.pipeline_window + 1) × request.temp_init_kb × 并发连接数预估` | 暂存区抢占 | warn |
| V-07 | `worker.per_group ≥ 2`（若要启用"卡住换线程"） | `ThreadPoolInit(totalNum<=1)` 直接返回 0，`threadPool` 为 NULL，`ReveiverThreadMain` 里 `t->workers[...]` **空指针** | 拒绝启动 或 强制 =2 |
| V-08 | `listen.backlog ≤ /proc/sys/net/core/somaxconn` | 内核静默截断，压测时连接被拒 | warn 并打印实际生效值 |
| V-09 | `group.num × (1 + worker.per_group) ≤ CPU 核数 × 2` | 线程互相抢核，QPS 反而降 | warn |
| V-10 | `epoll.tick_ms ≤ worker.stall_ms` | 换线程判定粒度比事件循环还细，判定永远滞后一个 tick | warn |

V-07 是个隐藏地雷：`ThreadPool.c:42` 只要 `totalNum<=1` 就**提前 return 0**，而 `:45` 的 `receiver->threadPool = NULL;` 根本执行不到；`ReceiverThreadInit` 里 `Receiver_Thread` 是裸 `malloc`（没清零），于是 `ReceiverThread.c:123-124` 的 `t->workers[t->mainThread]` 会解引用一个**未初始化指针**（比空指针更隐蔽，可能不崩而是随机行为）。所以 `worker.per_group` 的下界要么锁 2，要么先把 `ThreadPoolInit` 改成"totalNum≤1 时自己兜底建 1 个 worker"。

> 本机参考值（写报告时的测量机）：`/proc/sys/net/core/somaxconn = 4096`，`nproc = 3`。
> 也就是说 `listen.backlog` 配到 512 是安全的，但 `group.num × (1+worker.per_group)` 超过 6 就已经在抢核了（V-09 会 warn）。

---

## 6. Config 模块设计

### 6.1 文件与结构

```
Config/
├── CWebConfig.h     // 结构体 + 默认值 + API
└── CWebConfig.c     // 解析、环境变量覆盖、校验、dump
```

```c
// Config/CWebConfig.h
#ifndef CWEB_CONFIG_H
#define CWEB_CONFIG_H

typedef struct CWebConfig{

    /* ---- 监听/站点 ---- */
    char listen_addr[64];
    int  listen_port;
    int  listen_backlog;
    char web_root[256];

    /* ---- 线程模型 ---- */
    int  group_num;
    int  worker_per_group;

    /* ---- 接待线程 ---- */
    int  recv_idle_timeout_s;   // 连接空闲超时
    int  recv_epoll_events;     // epoll_wait 单次上限
    int  recv_send_batch;       // 每个事件最多连发几包
    int  timer_capacity;        // 计时器节点数（≥ 最大连接数）
    int  fd_table_init_level;   // 1..5

    /* ---- 队列/背压 ---- */
    int  task_queue_capacity;
    int  temp_pack_capacity;
    int  send_pipeline_window;

    /* ---- 内存池配额（字节；当前仅统计） ---- */
    int  pool_recv_http;
    int  pool_recv_store;
    int  pool_worker_txt;
    int  pool_worker_http;

    /* ---- HTTP 限额（字节） ---- */
    int  req_max_url;
    int  req_max_header;
    int  req_max_body;
    int  req_max_total;
    int  req_max_header_fields;
    int  req_temp_init_kb;
    int  req_temp_pool_max;

    /* ---- worker 等待/响应 ---- */
    int  worker_stall_ms;
    int  worker_sem_wait_ms;
    int  worker_idle_sleep_us;
    int  rsp_header_max;

    /* ---- 其它 ---- */
    int  epoll_tick_ms;
    int  error_buf_bytes;

} CWebConfig;

extern CWebConfig g_cfg;                        // 只在启动路径读它

int  CWebConfigSetDefaults(CWebConfig* c);      // 全部填成宏默认值
int  CWebConfigLoadFile(CWebConfig* c,const char* path);   // key = value
int  CWebConfigApplyEnv(CWebConfig* c);         // CWEB_xxx 覆盖
int  CWebConfigValidate(const CWebConfig* c);   // §5 的规则，失败返回 -1 并打印
void CWebConfigDump(const CWebConfig* c);       // 启动时打印生效配置

// 一步到位：默认 → 文件(可为 NULL) → 环境变量 → 校验
int  CWebConfigInit(const char* path);

#endif
```

### 6.2 加载顺序与兼容

```
CWebConfigSetDefaults()      // 宏值：保证"不配置=现在行为"
      ↓
CWebConfigLoadFile(path)     // path 来自 CWEB_CONF 环境变量，或 argv -f，缺省 /etc/cweb.conf
      ↓
CWebConfigApplyEnv()         // CWEB_LISTEN_PORT=... 覆盖；并兼容 CWEB_GROUP_NUM / CWEB_WORKER_NUM / CWEB_PARALLEL
      ↓
CWebConfigValidate()         // §5 规则；失败 print + exit(1)
      ↓
CWebConfigDump()             // 逐行打印 "+ listen.port = 5343"
```

**环境变量优先级最高**，理由是容器/K8s 场景改 env 比挂配置文件方便；同时让现有 `CWEB_GROUP_NUM` 这类用法继续有效。

### 6.3 解析器要求（不要引三方库）

- 只支持 `key = value`，`#` 或 `;` 起注释，允许行尾空白；键名大小写不敏感
- 未知键 → **warn 并继续**（便于旧配置平滑升级）；拼写错误的键会 warn，比静默忽略好
- 数值用 `strtol` + `endptr` 校验，**不要用 `atoi`**（`atoi("12x")` 返回 12，"abc" 返回 0，都会把错配置吃进去）
- 字符串值去掉首尾空白，长度超界 → 拒绝启动（不要静默截断）
- 单文件上限（如 64KB），防止误把大文件当配置

### 6.4 线程模型：热路径不许读 `g_cfg`

`g_cfg` 是全局结构体，多个线程反复读同一行会带来不必要的内存依赖（还容易在改配置时引入数据竞争）。做法是**启动时拷进各自的上下文**：

| 读取点 | 目标字段 |
|---|---|
| `WebStater()` / `StartThread()` | `listen_*`、`group_num`、`worker_per_group` |
| `ReceiverThreadInit()` | `recv_*`、`timer_capacity`、`fd_table_init_level`、`pool_recv_*` |
| `WorkerThreadInit()` | `pool_worker_*`、`worker_*`、`rsp_header_max` |
| `RequestComeMain()` / `RequestAnalysiser()` | `req_*`（这些在热路径上，**必须**拷成 `Receiver_Thread` 里的字段或 `static const`） |

`Receiver_Thread` 里已经有 `id/epfd/...`，加几个 int 字段不心疼（`sizeof(Receiver_Thread)` 现在是 96B）。

### 6.5 API 兼容

- `WebStater(ip, port, handler)` **保持不变**：内部先 `CWebConfigInit(NULL)`，`ip/port` 若非 NULL/非 0 则覆盖配置（保持老调用方的语义）
- 新增 `WebStaterWithConfig(const CWebConfig* cfg, RouterFunc handler)`
- `AgentSpace/main.c` 改成：读 `CWEB_CONF`/默认路径 → `WebStaterWithConfig()`
- `bench/bench_main.c:45` 同理（它现在直接传端口，见 G-04）


---

## 7. 配置文件样例（`cweb.conf`）

```ini
# cweb.conf —— 所有键都可省略，省略即用宏默认值（= 现在的行为）
# 单位写在键名里；# 起注释

# ---------- 监听 / 站点 ----------
listen.addr            = 0.0.0.0
listen.port            = 5343
listen.backlog         = 512
web.root               = /var/www/cweb

# ---------- 线程模型 ----------
group.num              = 2          # 一组 = 1 接待线程 + N worker
worker.per_group       = 4          # 必须 >= 2（见 V-07）

# ---------- 接待线程 ----------
receiver.idle_timeout_s = 60
receiver.epoll_events   = 1024
receiver.send_batch     = 100
timer.capacity          = 20000     # >= 单组最大连接数（V-01）
fd_table.init_level     = 4         # 1..5

# ---------- 队列 / 背压 ----------
task_queue.capacity     = 256
temp_pack.capacity      = 1024      # 需先改结构（§9-1）
send.pipeline_window    = 128

# ---------- 内存池配额（当前只做统计/告警） ----------
pool.recv_http_kb       = 1024
pool.recv_store_kb      = 2048
pool.worker_txt_kb      = 2048
pool.worker_http_kb     = 256

# ---------- HTTP 限额 ----------
request.max_url_bytes      = 8192
request.max_header_bytes   = 16384
request.max_body_bytes     = 1048576
request.max_total_bytes    = 1114112  # >= header + body（V-03）
request.max_header_fields  = 128
request.temp_init_kb       = 2
request.temp_pool_max_bytes = 1048576

# ---------- worker ----------
worker.stall_ms         = 4
worker.sem_wait_ms      = 100
worker.idle_sleep_us    = 100000
response.header_max_bytes = 4096

# ---------- 其它 ----------
epoll.tick_ms           = 2
error.buf_bytes         = 1024
```

环境变量等价写法（优先级更高）：

```bash
CWEB_LISTEN_PORT=8080 CWEB_GROUP_NUM=4 CWEB_WORKER_NUM=8 ./main
# CWEB_WORKERS_PER_GROUP 与 CWEB_WORKER_NUM 二选一，推荐前者（名字更准）
```

---

## 8. 分阶段落地（每批都能单独回滚）

| 批次 | 内容 | 影响面 | 验证 |
|---|---|---|---|
| **第 0 批（前置）** | 修 §9-1 ~ §9-5 的 5 个现存问题；A-01（worker 数）、A-05（桶数组）、`/api/info` 端口必须先修 | 行为有修正 | 编译 + 线程数/M 桶数对比 + 单测 + QPS |
| **第 1 批** | 新增 `Config/CWebConfig.{c,h}`（只加不改）：默认值 = 现有宏，空路径时行为 100% 等价 | 纯新增 | 启动日志逐项对比默认值 |
| **第 2 批** | 接 P0 前 9 项（监听/站点/线程模型/计时器/超时），都不在每请求热路径 | 启动路径 | `ss -lnt`、`ps -o nlwp`、61s 实测断连 |
| **第 3 批** | 接 P0 后 8 项（池配额 + HTTP 限额）；限额在热路径，拷进 `Receiver_Thread` | 每请求 | 造超限请求验 414/431/413 |
| **第 4 批** | 接 P1（队列容量/批处理/等待时间/epoll 粒度/错误缓冲） | 调优面 | `make qps` + 503/408 计数 |
| **第 5 批** | `temp_pack.capacity` 先改结构（指针 + 容量），单独一批 | 每连接 | valgrind + 队列满场景 |

每批结束都跑：`make` → `bash AgentSpace/test/run_tests.sh` → `bash AgentSpace/test/website_test.sh`。


---

## 9. 风险与坑

### 9-1 `TEMP_PACK_MAX` 是结构体数组长度，不能直接配置

- 现状：`Threads/ReceiverThread.h:19` `Temp_Pack_Entry e[TEMP_PACK_MAX];`，实测 `sizeof(Temp_Pack)=32784`（每组常驻 32KB）。
- 想配置必须改结构：

```c
typedef struct Temp_Pack{
    int head, tail, count, cap;
    Temp_Pack_Entry* e;          // Init 时 malloc(cap * sizeof(...))
}Temp_Pack;
```
- 触点：`ReceiverThread.c:19,29,48`（用 `TEMP_PACK_MAX` 判满/取模）→ 改成 `tempPack->cap`；`TempPackInit` 增加 cap 参数。
- 风险：`Temp_Pack` 不再内嵌，释放路径要补（`FdConnectOrClose` 里现在没有它的销毁函数）。

### 9-2 池配额不是硬上限（最容易被误读）

MemoryPool 现在是 malloc + 16B 头，`quotaBytes` 只进 `MemoryPoolStat`。所以 `pool.*` 配得再小也不会 503；§5 里的 V-05/V-06 现在只是推理，不会真的触发。想要"真限额"，得先回到真池（`PERF_TODO.md` P0-2 方案 A）。

### 9-3 `FdTableInit(&t,4)` 传的是"阶梯下标"

`FdConnectTable.c:4-11` 的 `PRIME_BUCKET_SIZES_3[15]` 只初始化了 5 项（7/17/31/59/113），内部用 `PRIME_BUCKET_SIZES_3[init-1]`。所以配置项要么暴露"目标桶数"、要么锁死 1..5 并在 `FdTableInit` 里检查；否则传 6 会拿到 0 当桶数，取模直接除零。

### 9-4 两套 HTTP 限额要合并

`MAX_BODY_SIZE`（1MB，`RequestIntergrity.c:4`）和 `MAX_REQUEST_BYTES`（1088KB，`RequestComeMain.c:4`）是两处独立阈值，语义重叠。配置化时统一成 `max_body / max_header / max_url / max_total` 四个数 + V-03 校验，否则会出现"文档说 1MB、实际 1088KB 就断"这类不一致。

### 9-5 死声明与"注入了但没接上"

- `Include/Variate.h:49` `extern int taskQueueBlocknum;`：全仓库没有定义，注释却说"GlobalResourceInit 里设置"。配置化时**要么删，要么让它成为 `task_queue.capacity` 的唯一出口**（推荐后者，免得又多一个全局 int）。
- `StarterMain.c:118` 的 `workers` 只用于打印（A-01），是"注入不生效"的典型：不先修，加配置也没用。
- `AgentSpace/WebApp/WebApp.c:207` 把端口 5343 写进 JSON，配置化后必须读 `g_cfg.listen_port`，否则 `/api/info` 会返回错数据。

### 9-6 类型与溢出

容量/字节数保持 `int`（与现状一致）；`pool.*_kb × 1024` 在 int 里上限 2GB，所以把配置上限卡在 65536 KB（64MB）就永远不会溢出，也不会撞到 `MemoryPoolStat` 的 int 参数。

### 9-7 运行期热更新（明确不做）

`*_ms`、`*_timeout_s` 这类"下次判断时读一次"的值理论可以用 `_Atomic int` 热更新；但**容量类（队列/池/计时器/fd 表）只能重启生效**。建议写进文档，别让人以为改了文件就生效。

---

## 10. 验证清单（怎么确认"真的注入了"）

| # | 验证点 | 方法 | 期望 |
|---|---|---|---|
| 1 | 生效配置已打印 | 启动日志 | 逐行 `+ key = value`，与配置文件一致 |
| 2 | 端口/backlog | `ss -lntp \| grep <port>` | 端口正确；backlog 被内核截断时打印实际值（V-08） |
| 3 | 线程数 | `ps -o nlwp= -p <pid>` | `1 + group.num × (1 + worker.per_group)` |
| 4 | 空闲超时 | `nc <ip> <port>` 连上不发数据，等 `idle_timeout_s+2` | 服务端关闭连接（408） |
| 5 | HTTP 限额 | 超长 URL / 超多 header / 超大 body | 414 / 431 / 413 与配置阈值一致 |
| 6 | 队列容量 | `task_queue.capacity=8` 后并发压测 | 出现 503（背压），但不崩、不死锁 |
| 7 | 环境变量覆盖 | `CWEB_LISTEN_PORT=8080 ./main` | 监听 8080，日志 `listen.port = 8080` |
| 8 | 未知键 | 配置里写 `listen.prot = 1` | warn 未知键，仍正常启动 |
| 9 | 越界值 | `timer.capacity = 1` | 拒绝启动并打印 V-01 |
| 10 | 默认等价 | 不带配置文件启动 | 逐项等于改动前（用第 1 批的打印对比） |

---

## 附录 A：不建议配置化（保持宏或裸常量）

| 类别 | 例子 | 原因 |
|---|---|---|
| 协议常量 | 状态码 `408/413/414/431/503`、`StatusText()` 表、方法表、`\r\n\r\n` 长度 | 配错就是协议错乱 |
| 块内布局 | `*_QUEUE_ALLOC_BYTES`、`*_NODE_OFFSET` | 依赖 `sizeof(struct)`，只有编译期有意义 |
| 哈希阶梯 | `PRIME_BUCKET_SIZES_3[]`、`ANALYSISER_HASH_BUCKETS` | 算法常量；配置只暴露"初始级别" |
| 小格式化缓冲 | `char buf[32]`（Content-Length）、`path[64]`、`buf[512]` | 与具体 `snprintf` 绑定，配错只会截断 |
| 状态机内部编号 | `h_method = 1..9` | 内部枚举 |
| 编译开关 | include 集合、`-O2 -march=native` | Makefile 管 |
| 示例/测试值 | `WebApp.c:28 MEM_BODY_SIZE 256KB`、`bench/run_qps.sh` 的并发/时长 | 示例代码，跟着脚本走（宏报告 G-02/G-05） |

## 附录 B：与 `CONSTANT_MACRO_REPORT.md` 的编号对照

| 本报告 | 宏报告对应项 | 关系 |
|---|---|---|
| §4.1-5 / 4.1-6 | A-01、C-04 | 先修 A-01，否则 `worker.per_group` 不生效 |
| §4.1-8 | A-05、C-10、F-06 | 先修桶数组初始化，再加范围检查 |
| §4.1-14~17 | E-01~E-04、I-06 | 四个限额合并成一组 + V-03 |
| §4.1-10~13 | D-01~D-04 | 宏报告只要求宏化，本报告要求可注入 |
| §4.2-18 / 19 | C-01、C-02 | 19 需先改结构（§9-1） |
| §4.2-20 / 21 / 30 | C-05、C-06 | 热路径，拷进 `Receiver_Thread` |
| §4.2-23/24/25 | C-03、C-07、C-08 | 建议统一成 `worker.*` 前缀 |
| §4.2-28 | D-07、A-03 | 注释与实际值不一致，一起改 |
| §9-2 | I-01 | 同一件事：池不是硬上限 |
| §9-5 | A-01、A-05 | 死声明 / 注入不生效 |

## 最后提醒

1. **默认值必须等于现在的值**：这是整套方案的退路，任何一批出问题，去掉配置文件就回到今天的行为。
2. **配置只在启动期读一次**，热路径不要出现 `g_cfg.xxx`（拷进线程自己的上下文）。
3. **能拒绝启动就别 clamp**：容量类静默变小，最后都会变成线上"偶发 503 / 偶发不超时"。
4. 先做第 0 批的 5 个现存问题，否则你会以为"配了没用"是配置模块的问题。

