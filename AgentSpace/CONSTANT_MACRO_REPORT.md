# c_web 常量宏化检查报告

- 检查日期：2026-10-27
- 检查范围：`GlobalResource`、`Threads`、`Queue`、`RequestCome`、`RequestAndResponse`、`ResponseSend`、`Router`、`FdConnectManage`、`FdConnectResource`、`FdConnectOrClose`、`StaterMain`、`MemoryPool`、`ThreadPool`、`MyLock`、`AgentSpace`、`bench`
- 未检查：`MemoryPool/OldPool`、`AgentSpace/test`、生成物、文档中的历史数字
- 本次只读检查，**没有修改任何源码**；本文件是给你逐项修改用的清单。

> 行号以当前工作区为准。你开始改之后行号会漂移，后续请以“符号/描述”定位，不要只认行号。

---

## 0. 怎么用这份报告

每一项都有编号（`A-01`、`C-03`……），建议按编号顺序改，一项一个 commit 或一组一个 commit。

每项包含：

- **位置**：文件:行
- **现在**：当前硬编码值/现状
- **建议**：建议的宏名或处理方式
- **原因**：为什么值得宏化
- **改法**：可以直接照抄的写法（报告里是建议，不是已落地的代码）
- **验证**：这一项改完最少要跑什么
- **风险**：容易踩的坑

建议的提交批次：

1. **A 组**：先修“配置失效/不一致”，否则后面宏化了也不生效
2. **B 组**：建立统一配置头（纯新增，不改行为）
3. **C 组**：线程、队列、事件、超时参数
4. **D 组**：内存池/缓冲区大小
5. **E 组**：HTTP 限制/协议常量
6. **F 组**：哈希表容量
7. **G 组**：应用/压测入口
8. 每组结束跑一次编译 + 单测；C/D 组结束跑一次 QPS 对比

---

## A. 先修配置失效/不一致（最高优先级）

这些不是“缺宏”，而是现有配置没有真正生效，或者宏和实际行为对不上。**先改这一组**，否则后面把 `WORKER_THREAD_NUM_*` 宏化也不会有效果。

### [ ] A-01 `CWEB_WORKER_NUM` 实际不生效，日志也打印错

- **位置**：
  - `StaterMain/StarterMain.c:103-112`：`WorkerThreadNum()` 读取 `CWEB_WORKER_NUM`，默认 `WORKER_THREAD_NUM_DEFAULT`
  - `StaterMain/StarterMain.c:118`：`int workers=WorkerThreadNum();`
  - `StaterMain/StarterMain.c:121`：打印 `workers`
  - `StaterMain/StarterMain.c:136`：实际传的是 `PER_RECEIVER_WORKER`
- **现在**：
  - `PER_RECEIVER_WORKER` = 4（`GlobalResource/GlobalResource.h:8`）
  - `WORKER_THREAD_NUM_DEFAULT` = 2（`GlobalResource/GlobalResource.h:9`）
  - 实际每组创建 4 个 worker，但日志打印 2；`CWEB_WORKER_NUM` 改了没用
- **建议**：
  - 二选一，不要同时保留两套 worker 配置：
    - 方案 1：`ReceiverThreadInit(&w, fd, i+1, workers);`，让 `CWEB_WORKER_NUM` 真正生效
    - 方案 2：删掉 `WorkerThreadNum()` 和 `WORKER_THREAD_NUM_*`，只保留 `CWEB_WORKERS_PER_GROUP`
  - 推荐方案 2，名字更准确：`CWEB_WORKERS_PER_GROUP`，默认 4
- **改法（方案 2 示例）**：
  ```c
  // GlobalResource/GlobalResource.h
  #ifndef CWEB_WORKERS_PER_GROUP
  #define CWEB_WORKERS_PER_GROUP 4
  #endif
  ```
  ```c
  // StaterMain/StarterMain.c
  int workers = CWEB_WORKERS_PER_GROUP;   // 或从 env 读，但只保留这一处
  printf("[WebStater] 组数=%d，每组接待线程=1，每组工作线程=%d\n", group, workers);
  ...
  ReceiverThreadInit(&w, fd, i+1, workers);
  ```
- **注意**：`ThreadPoolInit()`（`ThreadPool/ThreadPool.c:41-44`）在 `totalNum <= 1` 时直接返回 0，不创建 worker。若允许配置为 1，需要同时改 `ThreadPoolInit` 或把下限限制为 2。
- **验证**：
  - 启动日志里的 worker 数 = 实际线程数
  - `CWEB_GROUP_NUM=2` 启动后，用 `ps -L`/`/proc/<pid>/task` 数线程：应为 `2 * (1 + workers)` 左右
  - `CWEB_WORKER_NUM=8`（如果保留 env）要能看到实际变化
- **风险**：改错会导致 worker 数为 1，`ThreadPoolInit` 返回 0，任务无人消费。

### [ ] A-02 `CWEB_PARALLEL=0` 仍然会开启并行

- **位置**：`ThreadPool/ThreadPool.c:62`
- **现在**：
  ```c
  if(getenv("CWEB_PARALLEL") != NULL){
      for(int i=0;i<totalNum;i++) wakeThread(t->workers[i]);
  }else{
      wakeThread(t->workers[0]);
  }
  ```
  判断的是“环境变量存在”，不是“值为真”。
- **建议**：
  - 增加 `CwebEnvBool()`，解析 `1/0/true/false/yes/no`
  - 或者至少改成：
    ```c
    const char* p = getenv("CWEB_PARALLEL");
    int parallel = (p != NULL && p[0] != '\0' && strcmp(p, "0") != 0);
    ```
- **验证**：
  - `unset CWEB_PARALLEL`：只有 `worker[0]` 被唤醒
  - `CWEB_PARALLEL=1`：全部 worker 被唤醒
  - `CWEB_PARALLEL=0`：只有 `worker[0]` 被唤醒
- **风险**：这是行为变更；先确认业务是否依赖“设置任意值即开启”的旧行为。

### [ ] A-03 `RSP_HEADER_BLOCK` 注释与实际值不一致

- **位置**：`Threads/WorkerThread.c:4-6`
- **现在**：
  ```c
  //响应头块大小：16KB 足够（实际头部只有几百字节）；写满会由 ResponseHeaderSet 回 503
  #define RSP_HEADER_BLOCK (4*1024)
  #define RSP_FIRST_LINE 17
  ```
  注释说 16KB，实际 4KB。
- **建议**：
  - 改注释为 4KB，或把值改成 16KB；**不要只改一个**
  - 宏改名：`CWEB_RSP_HEADER_BYTES`、`CWEB_RSP_FIRST_LINE_BYTES`
- **验证**：`ResponseHeaderSet` 超限仍返回 503；网站集成测试通过。
- **风险**：如果直接把 4KB 改 16KB，每个 worker 的响应池占用会上升；如果只改注释，行为不变。

### [ ] A-04 Router 哈希初始级别和扩容级别对不上

- **位置**：
  - `Router/Router.c:6`：`RouterHashInit(&e,2)`
  - `Router/RouterDataStruct/RouterHash.c:24`：`PRIME_BUCKET_SIZES_nidex=0`
  - `RouterHash.c:140,144`：扩容取 `PRIME_BUCKET_SIZES[PRIME_BUCKET_SIZES_nidex+1]`
- **现在**：
  - 初始用 `PRIME_BUCKET_SIZES[1] = 17`
  - 第一次扩容仍取 `PRIME_BUCKET_SIZES[1] = 17`，**第一次扩容不会变大**
  - 第二次才到 31
- **建议**：
  - 用 `CWEB_ROUTER_HASH_INIT_LEVEL` 统一初始级别
  - 让 `PRIME_BUCKET_SIZES_nidex` 跟随初始级别，或把扩容函数改为“从当前桶数找下一个更大值”
  - 增加 `CWEB_ROUTER_HASH_MAX_LEVEL`，防止 `nidex+1` 越界
- **验证**：路由注册超过 `17*2` 条后，桶数应从 17 → 31，而不是 17 → 17。
- **风险**：当前 WebApp 只有 14 条路由，不会触发；但这是未来扩容的坑。

### [ ] A-05 FdConnectTable 的桶数组只初始化了 5 项，却声明 15 项

- **位置**：`FdConnectManage/FdConnectTable/FdConnectTable.c:4-11`
- **现在**：
  ```c
  static const int PRIME_BUCKET_SIZES_3[15] = {
      7, 17, 31, 59, 113,
  };
  ```
  后面的 10 个元素默认是 0。
- **调用**：`Threads/ReceiverThread.c:70` 传 `4`，当前落在 `113` 之前，安全。
- **建议**：
  - 增加 `CWEB_FD_TABLE_INIT_LEVEL`，默认 4
  - 增加 `CWEB_FD_TABLE_LEVELS` 或 `ARRAY_SIZE(PRIME_BUCKET_SIZES_3)`
  - `FdTableInit` 里校验 `init >= 1 && init <= LEVELS`，越界返回 -1
  - 如果以后要支持更多连接，补齐表项或改成动态扩容
- **验证**：`ReceiverThreadInit` 正常；连接数超过初始桶负载后，链表仍正常（当前无扩容，属于容量限制）。
- **风险**：以后把初始级别调大，可能读到 0，除零或桶数为 0。

### [ ] A-06 `sleepInit` / `sleepAdd` 是未使用代码

- **位置**：`Threads/WorkerThread.c:111-126`
- **现在**：函数没有被任何地方调用，里面还有 `1000000`、`100000` 两个魔法数。
- **建议**：确认无引用后删除；如果保留，改为 `CWEB_SLEEP_STEP_US` 等宏并注明用途。
- **验证**：`grep -R "sleepInit\\|sleepAdd"` 无调用；编译无告警。
- **风险**：低；只是清理。

---

## B. 建立统一配置头（建议先做，纯新增）

### [ ] B-01 新建 `Include/CWEBConfig.h`

- **目的**：把“可调参数”集中到一处，避免 `MAX_*`、`REQUEST_*`、`RSP_*`、`TEMP_*` 散落在各 `.c`。
- **建议写法**：所有编译期可覆盖项都用 `#ifndef` 包住：

```c
#ifndef CWEB_CONFIG_H
#define CWEB_CONFIG_H

/* ---- 并发/线程 ---- */
#ifndef CWEB_WORKERS_PER_GROUP
#define CWEB_WORKERS_PER_GROUP 4
#endif

#ifndef CWEB_GROUP_NUM_MAX
#define CWEB_GROUP_NUM_MAX 32
#endif

/* ---- 队列/事件 ---- */
#ifndef CWEB_TASK_QUEUE_CAP
#define CWEB_TASK_QUEUE_CAP 100
#endif

#ifndef CWEB_TEMP_PACK_CAP
#define CWEB_TEMP_PACK_CAP 1024
#endif

#ifndef CWEB_EPOLL_MAX_EVENTS
#define CWEB_EPOLL_MAX_EVENTS 1024
#endif

#ifndef CWEB_EPOLL_TICK_MS
#define CWEB_EPOLL_TICK_MS 2
#endif

/* ---- 超时/窗口 ---- */
#ifndef CWEB_IDLE_TIMEOUT_SEC
#define CWEB_IDLE_TIMEOUT_SEC 60
#endif

#ifndef CWEB_SEND_WINDOW
#define CWEB_SEND_WINDOW 100
#endif

#ifndef CWEB_TASK_WAIT_MS
#define CWEB_TASK_WAIT_MS 100
#endif

/* ---- 尺寸 ---- */
#define CWEB_KB (1024)
#define CWEB_MB (1024 * CWEB_KB)
/* 注意：n=0 时返回 1KB，保持现有“至少分配 1KB”的语义 */
#define CWEB_ROUND_UP_KB(n) \
    (((n) > 0) ? ((((n) + CWEB_KB - 1) / CWEB_KB) * CWEB_KB) : CWEB_KB)

#endif /* CWEB_CONFIG_H */
```

- **注意**：
  - `CWEB_ROUND_UP_KB` 的 `n` 应是整数；如果参数可能超过 `INT_MAX`，改用 `size_t` 版本。
  - `n=0` 的语义要保留：现有 `1024*(len/1024+1)` 对 `len=0` 会分配 1KB；宏必须返回至少 1KB，否则 `MemoryPoolAlloc(needSize=0)` 会失败。
  - 现有名字（`TASK_QUEUE_NUM`、`TEMP_PACK_MAX`）可以暂时保留为别名：
    ```c
    #define TASK_QUEUE_NUM CWEB_TASK_QUEUE_CAP
    ```
    但最终建议只保留一个名字，避免两处漂移。
- **验证**：只新增头文件、在 `Include/Include.h` 里包含它，编译应完全不变。

### [ ] B-02 增加环境变量解析 helper，统一 `atoi`

- **位置建议**：`GlobalResource/GlobalResource.c` 或新建 `GlobalResource/Config.c`
- **现在**：`StaterMain/StarterMain.c:92,105` 直接用 `atoi`，没有上下限；`CWEB_PARALLEL` 用存在性判断。
- **建议**：
  ```c
  int CwebEnvInt(const char* name, int def, int min, int max);
  int CwebEnvBool(const char* name, int def);
  ```
  - `CwebEnvInt`：无变量/空串/非法 → 返回 `def`；结果 clamp 到 `[min,max]`
  - `CwebEnvBool`：`1/true/yes/on` → 1；`0/false/no/off` → 0；空/无 → `def`
- **验证**：
  - `CWEB_GROUP_NUM=0` → 1
  - `CWEB_GROUP_NUM=999` → 32
  - `CWEB_PARALLEL=0` → false
  - `CWEB_PARALLEL=abc` → 默认 false
- **风险**：低；注意不要在每次任务里调用 `getenv`，只在启动/初始化时读。

### [ ] B-03 宏命名约定

- **建议统一前缀**：`CWEB_<AREA>_<PARAM>`
  - 并发：`CWEB_THREAD_*`
  - 接收：`CWEB_RX_*`
  - worker：`CWEB_WORKER_*`
  - HTTP：`CWEB_HTTP_*`
  - 队列：`CWEB_QUEUE_*`
  - 内存：`CWEB_POOL_*`
  - 哈希：`CWEB_HASH_*`
- **原则**：
  - 容量/字节数用 `_BYTES` / `_CAP` / `_SEC` / `_MS` 后缀
  - 不要在宏名里写具体值（不要 `CWEB_QUEUE_100`）
  - `PRIME_BUCKET_SIZES` 这类表继续用 `static const int`，不要整张表做成宏

---

## C. 线程、队列、事件、超时参数

### [ ] C-01 任务队列容量

- **位置**：`GlobalResource/GlobalResource.h:3`；使用点 `Threads/ReceiverThread.c:67`
- **现在**：`#define TASK_QUEUE_NUM 100`
- **建议**：`CWEB_TASK_QUEUE_CAP`，默认 100；保留 `TASK_QUEUE_NUM` 兼容别名
- **原因**：每组一条队列，容量影响背压和内存；压测时经常要调
- **改法**：
  ```c
  #ifndef CWEB_TASK_QUEUE_CAP
  #define CWEB_TASK_QUEUE_CAP 100
  #endif
  ```
- **验证**：现有 `AgentSpace/test/src/test_queues.c` 的容量测试；满队列 push 返回 0
- **风险**：调大后每组多占 `2 * cap * sizeof(Task_Queue_Entry)`；32 组时注意内存

### [ ] C-02 任务队列满时的暂存环

- **位置**：`Threads/ReceiverThread.h:4`
- **现在**：`#define TEMP_PACK_MAX 1024`
- **建议**：`CWEB_TEMP_PACK_CAP`，默认 1024
- **原因**：`Temp_Pack` 是内嵌数组（`ReceiverThread.h:19`），容量直接决定 `Receiver_Thread` 大小
- **改法**：`#ifndef CWEB_TEMP_PACK_CAP` 包住；`Temp_Pack_Entry e[CWEB_TEMP_PACK_CAP];`
- **验证**：队列满、暂存环满时仍返回 503，不越界
- **风险**：这个值太大时，每组接待线程结构体会变大；它和 `CWEB_TASK_QUEUE_CAP` 应一起评估

### [ ] C-03 worker 阻塞判定阈值

- **位置**：`GlobalResource/GlobalResource.h:5`；使用点 `Threads/ReceiverThread.c:130`
- **现在**：`#define ONE_TASK_TIME 4`，单位毫秒
- **建议**：`CWEB_ONE_TASK_MS`，默认 4
- **原因**：决定主 worker 阻塞多久后唤醒 standby；与 `CWEB_EPOLL_TICK_MS` 联动
- **验证**：慢接口 `/slow` 下观察 standby 是否按预期切换；QPS/延迟无回退
- **风险**：调太小会频繁切线程，调太大 standby 来不及接管

### [ ] C-04 组数/worker 数默认值与上限

- **位置**：`GlobalResource/GlobalResource.h:6-10`
- **现在**：
  ```c
  #define RECEIVER_GROUP_NUM_DEFAULT 1
  #define RECEIVER_GROUP_NUM_MAX     32
  #define PER_RECEIVER_WORKER 4
  #define WORKER_THREAD_NUM_DEFAULT 2
  #define WORKER_THREAD_NUM_MAX     16
  ```
- **建议**：
  - `CWEB_GROUP_NUM_DEFAULT`、`CWEB_GROUP_NUM_MAX`
  - `CWEB_WORKERS_PER_GROUP`
  - 删除或只保留一套 `WORKER_THREAD_NUM_*`（见 A-01）
- **原因**：现在 `CWEB_WORKER_NUM` 不生效，名字也容易误解
- **验证**：启动日志与实际线程数一致；`CWEB_GROUP_NUM` 边界 1/32 正常
- **风险**：线程总数 = `group * (1 + workers)`；32 组 × 5 线程 = 160 线程，注意调度和内存

### [ ] C-05 epoll 事件数组/单次返回上限

- **位置**：`Threads/ReceiverThread.c:117,122`
- **现在**：`struct epoll_event events[1024];` 和 `epoll_wait(..., 1024, ...)`
- **建议**：`CWEB_EPOLL_MAX_EVENTS`，默认 1024
- **原因**：每组的单次事件批量上限；高并发下可调
- **改法**：
  ```c
  struct epoll_event events[CWEB_EPOLL_MAX_EVENTS];
  n = epoll_wait(w->epfd, events, CWEB_EPOLL_MAX_EVENTS, CWEB_EPOLL_TICK_MS);
  ```
- **验证**：`AgentSpace/test/src/test_epoll.c`；QPS 压测无异常
- **风险**：数组在栈上，值太大可能栈溢出；建议不超过几 KB

### [ ] C-06 每次事件循环的发送批处理上限

- **位置**：`Threads/ReceiverThread.c:137`
- **现在**：`for(int i=0;i<100;i++)`
- **建议**：`CWEB_SEND_BATCH_PER_TICK`，默认 100
- **原因**：限制单次循环处理多少发送事件，影响公平性和延迟
- **验证**：QPS/延迟；`/slow` 与 `/fast` 混合下无饥饿
- **风险**：太大可能让单个 epoll 循环占用过久；太小增加循环次数

### [ ] C-07 worker 空队列等待时间

- **位置**：`Threads/WorkerThread.c:150`
- **现在**：`wait_ts.tv_nsec += 100000000;`（100ms）
- **建议**：`CWEB_TASK_WAIT_MS`，默认 100；用 `CWEB_TASK_WAIT_MS * 1000000LL`
- **原因**：这是“信号量提示偏差”时的最坏唤醒延迟；也是空闲 CPU 的折中
- **改法**：
  ```c
  wait_ts.tv_nsec += (long)CWEB_TASK_WAIT_MS * 1000000L;
  ```
- **验证**：空闲 CPU、QPS、P99；如果调到 1~10ms，观察空转
- **风险**：调太小会增加空闲唤醒和 CPU；调太大可能放大尾延迟

### [ ] C-08 队列未绑定时的休眠

- **位置**：`Threads/WorkerThread.c:143`
- **现在**：`usleep(100000);`
- **建议**：`CWEB_TASK_UNBOUND_SLEEP_US`，默认 100000
- **原因**：只在启动/绑定异常时使用，低优先级；但宏化后可避免魔法数
- **验证**：正常启动不会进入该分支；异常绑定不空转
- **风险**：低

### [ ] C-09 连接计时器初始容量

- **位置**：`Threads/ReceiverThread.c:69`
- **现在**：`TimerInit(&((*w)->my_Timer),5000);`
- **建议**：`CWEB_TIMER_INIT_CAP`，默认 5000
- **原因**：每组连接计时器堆的初始槽位；连接数大时需要调
- **验证**：大量连接/超时测试；`TimerExpend` 扩容正常
- **风险**：每组 5000 个 `Timer_Entry`，32 组时约 2.5MB（取决于结构体大小）

### [ ] C-10 连接哈希表初始级别

- **位置**：`Threads/ReceiverThread.c:70`
- **现在**：`FdTableInit(&((*w)->fd_table),4);`
- **建议**：`CWEB_FD_TABLE_INIT_LEVEL`，默认 4
- **原因**：决定初始桶数（当前 `PRIME_BUCKET_SIZES_3[3] = 59`）
- **验证**：连接数增长时链表长度可接受；见 A-05 的边界检查
- **风险**：调大占内存；调小链表变长

### [ ] C-11 listen backlog

- **位置**：`StaterMain/StarterMain.c:75`
- **现在**：`listen(*Fd, 100)`
- **建议**：`CWEB_LISTEN_BACKLOG`，默认 100
- **验证**：高并发建连测试；`ss -lnt` 观察 Send-Q
- **风险**：太大受内核 `somaxconn` 限制；太小会丢 SYN/拒绝

### [ ] C-12 连接空闲超时

- **位置**：`StaterMain/StarterMain.c:141`
- **现在**：`m1->time=60;`
- **建议**：`CWEB_IDLE_TIMEOUT_SEC`，默认 60
- **原因**：`TimerOvertime` 用秒比较；长连接/短连接场景值不同
- **验证**：`AgentSpace/test/src/test_timer.c`；连接空闲 60s 关闭
- **风险**：调太短会误杀慢请求/keep-alive；调太长会积压 fd

### [ ] C-13 每连接发送窗口

- **位置**：`StaterMain/StarterMain.c:142`
- **现在**：`m1->ed_store_blocknum=100;`
- **建议**：`CWEB_SEND_WINDOW`，默认 100
- **原因**：同时决定 `SendPointerPoolAlloc` 的 blocknum 和 `pack_in_path` 背压阈值
- **验证**：流水线/大文件下载；`pack_in_path` 不越界，`/download/10mb` 正常
- **风险**：调大增加每连接 send 指针池内存；调小限制流水线深度

---

## D. 内存池/缓冲区

> 先看报告最后的 **I-01**：当前 `MemoryPool` 是 malloc 过渡实现，`MemoryPoolInit` 的配额只用于统计，不会真正限制内存。下面的宏化只统一“参数入口”，不等同于硬上限。

### [ ] D-01 接待线程 HTTP 解析池

- **位置**：`Threads/ReceiverThread.c:60`
- **现在**：`MemoryPoolInit(&((*w)->http_pool),1024*1024,1);`（1MB/组）
- **建议**：`CWEB_RX_HTTP_POOL_BYTES`，默认 `1 * CWEB_MB`
- **改法**：
  ```c
  MemoryPoolInit(&((*w)->http_pool), CWEB_RX_HTTP_POOL_BYTES, 1);
  ```
- **验证**：请求解析、`/api/info`、`/echo`；QPS；内存统计不暴涨
- **风险**：每组独立，32 组时 1MB × 32 = 32MB 只是配额参数

### [ ] D-02 接待线程请求暂存池

- **位置**：`Threads/ReceiverThread.c:62`
- **现在**：`MemoryPoolInit(&((*w)->store_area),2048*1024,1);`（2MB/组）
- **建议**：`CWEB_RX_STORE_POOL_BYTES`，默认 `2 * CWEB_MB`
- **原因**：`RequestTempPoolAlloc/ExpendTo` 和 `SendPointerPoolAlloc` 都从这里分配
- **验证**：大请求、`/download/10mb`、连接反复建立释放；`RequestTempPool` 不 503
- **风险**：要和 `CWEB_REQUEST_TEMP_MAX_BYTES` 一起看；大请求超过池上限会走 malloc

### [ ] D-03 worker 响应池

- **位置**：`Threads/WorkerThread.c:103`
- **现在**：`MemoryPoolInit(&((*profes)->txt_pool),2048*1024,1);`（2MB/worker）
- **建议**：`CWEB_WORKER_TXT_POOL_BYTES`，默认 `2 * CWEB_MB`
- **原因**：每个 worker 独立；4 worker/组 × 32 组时是最大的内存项之一
- **验证**：`/mem/256k`、`/download/10mb`、QPS；worker 内存统计
- **风险**：宏化后不要误以为这是硬上限，见 I-01

### [ ] D-04 worker 请求解析池

- **位置**：`Threads/WorkerThread.c:107`
- **现在**：`MemoryPoolInit(&((*profes)->http_pool),256*1024,1);`（256KB/worker）
- **建议**：`CWEB_WORKER_HTTP_POOL_BYTES`，默认 `256 * CWEB_KB`
- **原因**：请求分析块按 1KB 向上取整分配；大 header/body 会走 malloc
- **验证**：`AgentSpace/test/src/test_parser.c`、`test_temp_pool.c`
- **风险**：和 `CWEB_MAX_REQUEST_BYTES`、`CWEB_MAX_HEADER_BYTES` 一起评估

### [ ] D-05 每连接初始请求暂存区

- **位置**：`RequestCome/RequestComeMain/RequestComeMain.c:29`
- **现在**：`RequestTempPoolAlloc(..., 2);`（单位 KB，即 2KB）
- **建议**：`CWEB_REQUEST_TEMP_INIT_KB`，默认 2
- **改法**：
  ```c
  int n = RequestTempPoolAlloc(fd_ob->request_temp_pool,
                               Receiver_Thread->store_area,
                               CWEB_REQUEST_TEMP_INIT_KB);
  ```
- **验证**：小请求、流水线请求、`test_temp_pool.c`
- **风险**：调大后每个活跃连接初始占用变大；调小会增加扩容次数

### [ ] D-06 请求暂存块走 malloc 的阈值

- **位置**：`FdConnectResource/RequestTempPool/RequestTempPool.c:4`
- **现在**：`#define REQUEST_TEMP_POOL_MAX (1024*1024)`（1MB）
- **建议**：`CWEB_REQUEST_TEMP_MAX_BYTES`，默认 `1 * CWEB_MB`
- **原因**：`RequestTempPoolExpendTo` 超过它就走 `malloc`，避免“旧块 + 新块”撑爆池
- **改法**：
  ```c
  #ifndef CWEB_REQUEST_TEMP_MAX_BYTES
  #define CWEB_REQUEST_TEMP_MAX_BYTES (1 * CWEB_MB)
  #endif
  ```
- **验证**：大 body、`test_temp_pool.c` 的扩容用例
- **风险**：必须小于/等于 `CWEB_RX_STORE_POOL_BYTES`，否则策略失效或频繁 malloc

### [ ] D-07 响应头块与首行长度

- **位置**：`Threads/WorkerThread.c:5-6`
- **现在**：
  ```c
  #define RSP_HEADER_BLOCK (4*1024)
  #define RSP_FIRST_LINE 17
  ```
- **建议**：
  - `CWEB_RSP_HEADER_BYTES`，默认 `4 * CWEB_KB`
  - `CWEB_RSP_FIRST_LINE_BYTES`，默认 17
- **注意**：注释写 16KB、值是 4KB（见 A-03）
- **验证**：`ResponseHeaderSet` 超限 503；网站集成测试
- **风险**：响应头块太大会增加每 worker 的 `txt_pool` 占用

### [ ] D-08 错误响应栈缓冲

- **位置**：`FdConnectOrClose/FdConnectOrClose.c:44-45`
- **现在**：`char re[1024]; BuildErrorResponse(re,1024,...)`
- **建议**：`CWEB_ERROR_RESPONSE_BUF`，默认 1024
- **原因**：错误响应体是固定状态文本，缓冲大小可配置
- **验证**：400/404/408/413/414/431/503 响应完整
- **风险**：调太小会截断状态行/头；调大增加栈占用

### [ ] D-09 Content-Length 文本缓冲

- **位置**：`Threads/WorkerThread.c:46`
- **现在**：`char buf[32];`
- **建议**：`CWEB_CONTENT_LENGTH_BUF`，默认 32
- **原因**：`snprintf` 生成 `Content-Length` 的十进制文本；32 足够 64 位
- **验证**：大响应/文件下载的 Content-Length 正确
- **风险**：低；不要小于 21（64 位十进制 + 符号/结尾）

### [ ] D-10 1KB 对齐/取整统一

- **位置**：
  - `RequestCome/RequestAnalysis/RequestAnalysiser.c:19,23`
  - `RequestAndResponse/Response.c:8`
  - `FdConnectResource/RequestTempPool/RequestTempPool.c:92`
  - `FdConnectResource/SendPointerPool/SendPointerPool.c:16,18`
- **现在**：各自写 `1024` / `1023`
- **建议**：统一用 `CWEB_KB` 和 `CWEB_ROUND_UP_KB(n)`
- **原因**：避免不同文件用不同的“KB”定义；便于以后改对齐粒度
- **验证**：`test_memory_pool.c`、`test_response.c`、`test_temp_pool.c`；QPS 不变
- **风险**：宏参数有副作用时不要写 `n++`；`CWEB_ROUND_UP_KB` 只接受纯值
- **特别注意**：`Response.c:8` 和 `RequestAnalysiser.c:19,23` 现在对 `0` 也会分配 1KB；改成取整宏时必须用“至少 1KB”的版本，否则空 body/空请求会分配失败

### [ ] D-11 ThreadPool 读取 `/proc` 的栈缓冲

- **位置**：`ThreadPool/ThreadPool.c:17`
- **现在**：`char path[64], buf[512];`
- **建议**：`CWEB_PROC_PATH_BUF`、`CWEB_PROC_STAT_BUF`（低优先级）
- **原因**：不是框架调优参数，但属于魔法数；路径长度受 PID 位数影响
- **验证**：standby 切换日志正常；`ThreadState()` 能读到 `R/S/D`
- **风险**：低；改太小可能截断 `/proc/.../stat`

### [ ] D-12 MemoryPool 头部常量

- **位置**：`MemoryPool/MemoryPool.c:16,25`
- **现在**：`MP_HEADER_MAGIC`、`MP_HEADER_BYTES`（16 字节对齐）
- **建议**：`CWEB_MP_HEADER_MAGIC`、`CWEB_MP_HEADER_ALIGN`
- **原因**：属于分配器内部布局；宏化便于调试/替换分配器
- **验证**：`test_memory_pool.c`、valgrind
- **风险**：不要随意改 magic；如果已有持久化/跨进程诊断依赖 magic，改值会破坏识别

---

## E. HTTP 限制/协议常量

### [ ] E-01 单请求总字节上限

- **位置**：`RequestCome/RequestComeMain/RequestComeMain.c:4`
- **现在**：`#define MAX_REQUEST_BYTES (1024*1024 + 64*1024)`（1MB + 64KB）
- **建议**：`CWEB_MAX_REQUEST_BYTES`，默认 `1 * CWEB_MB + 64 * CWEB_KB`
- **原因**：请求头和 body 的总上限；要能和 `CWEB_MAX_BODY_BYTES`、header 上限对上
- **改法**：
  ```c
  #ifndef CWEB_MAX_REQUEST_BYTES
  #define CWEB_MAX_REQUEST_BYTES (1 * CWEB_MB + 64 * CWEB_KB)
  #endif
  ```
- **验证**：`test_integrity.c`；超大请求返回 413
- **风险**：如果 body 上限改大而总上限没改，会被总上限提前 413；反之可能放行过大请求

### [ ] E-02 URL 上限

- **位置**：`RequestCome/RequestAnalysis/RequestAnalysiser.c:3`
- **现在**：`#define MAX_URL_LEN 8192`
- **建议**：`CWEB_MAX_URL_BYTES`，默认 `8 * CWEB_KB`
- **验证**：超长 URL 返回 414；正常路由不受影响
- **风险**：调大后 `MAX_REQUEST_BYTES` 要同步评估

### [ ] E-03 单个 header value 上限

- **位置**：`RequestCome/RequestAnalysis/RequestAnalysiser.c:4`
- **现在**：`#define MAX_HEADER_LEN 16384`
- **建议**：`CWEB_MAX_HEADER_BYTES`，默认 `16 * CWEB_KB`
- **注意**：这是“单个 header 值”的上限，不是所有 header 总和；总上限由 E-01 控制
- **验证**：超长 header 返回 431；`test_parser.c`
- **风险**：名字容易误解，宏注释里写清楚“per header value”

### [ ] E-04 body 上限

- **位置**：`RequestCome/RequestIntergrity/RequestIntergrity.c:4`
- **现在**：`#define MAX_BODY_SIZE (1024 * 1024)`（1MB）
- **建议**：`CWEB_MAX_BODY_BYTES`，默认 `1 * CWEB_MB`
- **验证**：`Content-Length > 1MB` 返回 413；正常 POST 通过
- **风险**：与 E-01 联动，见 E-01

### [ ] E-05 `Content-Length:` 前缀长度

- **位置**：`RequestCome/RequestIntergrity/RequestIntergrity.c:77`
- **现在**：`bodylength = bodylength + 15;`
- **建议**：`CWEB_HTTP_CONTENT_LENGTH_PREFIX_LEN`，默认 15
- **原因**：`"Content-Length:"` 固定 15 字节；宏化后可读性更好
- **验证**：POST body 长度解析正确
- **风险**：低；如果以后支持大小写/空格，逻辑本身要改，不是宏能解决的

### [ ] E-06 Content-Length 数字位数上限

- **位置**：`RequestCome/RequestIntergrity/RequestIntergrity.c:80,82`
- **现在**：`char length_num[100];` 和 `i < 99`
- **建议**：`CWEB_HTTP_CONTENT_LENGTH_MAX_DIGITS`，默认 99（数组为 +1）
- **原因**：避免魔法数 100/99；同时防止超长数字溢出
- **验证**：`Content-Length: 1048576` 正常；超长数字被截断/拒绝
- **风险**：调太小可能误拒合法长度；建议至少 20（64 位）

### [ ] E-07 `\r\n\r\n` 长度

- **位置**：
  - `RequestCome/RequestIntergrity/RequestIntergrity.c:154`：`target + 4 + len`
  - `RequestCome/RequestAnalysis/RequestAnalysiser.c:65`：`body_start += 4`
- **现在**：直接写 `4`
- **建议**：`CWEB_HTTP_HEADER_END_LEN`，默认 4
- **验证**：`test_integrity.c`、`test_parser.c`；GET/POST 都正确
- **风险**：低

### [ ] E-08 HTTP 方法表/索引

- **位置**：`RequestCome/RequestIntergrity/RequestIntergrity.c:5`、`:64-74`、`:145`
- **现在**：
  ```c
  char* Method_p[9]={"GET ","DELETE ","HEAD ","OPTIONS ","TRACE ","CONNECT ","POST ","PUT ","PATCH "};
  ```
  - `MethodMatch` 循环 `*sit < 9`
  - `strnstr_match(..., 8)` 的方法前缀最大长度
  - `h_method >= 7` 判断是否需要 Content-Length
- **建议**：
  - 用 `enum HttpMethod`，明确 `HTTP_METHOD_UNKNOWN = -1`、`GET=0`……或 `GET=1` 消除歧义
  - `CWEB_HTTP_METHOD_COUNT` / `ARRAY_SIZE(Method_p)`
  - `CWEB_HTTP_METHOD_PREFIX_MAX`，默认 8
- **重要提醒（当前代码的真实风险）**：
  - `h_method` 初始值是 `0`，同时又用 `0` 表示“还没找到”；而 `Method_p[0]` 是 GET，**GET 匹配后 h_method 仍是 0**，状态机会反复识别。
  - `h_method >= 7` 在 0-based 表里对应 PUT/PATCH，不包含 POST（POST 是 6）。如果意图是“POST/PUT/PATCH 都要求 Content-Length”，这个判断不对。
  - 建议这次宏化时顺便用 enum + 显式 UNKNOWN，不要只把 9/8/7 换成宏。
- **验证**：`test_integrity.c` 覆盖 GET/POST/PUT/PATCH；无 Content-Length 的 POST 应返回 411（按业务预期确认）
- **风险**：这是行为修正，建议单独提交，不要和纯宏替换混在一起

### [ ] E-09 方法判断的最小字节数

- **位置**：`RequestCome/RequestIntergrity/RequestIntergrity.c:110`
- **现在**：`if(check_size<=7){ *fail=-1; return 0; }`
- **建议**：`CWEB_HTTP_METHOD_MIN_BYTES`，默认 7（或按最短方法重新算）
- **原因**：这是“还不足以判断方法”的阈值，和 `Method_p` 的最短长度相关
- **验证**：分片请求逐字节到达时不会误判 400
- **风险**：改错会导致极短/分片请求被误判

### [ ] E-10 HTTP 状态码改 enum

- **位置**（大量散落）：
  - `Threads/WorkerThread.c:19,202,207,227,232,245,251,254,274,282,302,329,337`
  - `RequestCome/RequestComeMain/RequestComeMain.c:30,45,55,60,95,114`
  - `RequestCome/RequestIntergrity/RequestIntergrity.c:103,116,145,152`
  - `RequestCome/RequestAnalysis/RequestAnalysiser.c:93,103,108,121,138,151,154`
  - `FdConnectOrClose/FdConnectOrClose.c:41,43,73-82`
- **现在**：直接写 `200/400/404/408/411/413/414/431/500/503`
- **建议**：
  ```c
  typedef enum {
      HTTP_STATUS_OK = 200,
      HTTP_STATUS_BAD_REQUEST = 400,
      HTTP_STATUS_NOT_FOUND = 404,
      HTTP_STATUS_REQUEST_TIMEOUT = 408,
      HTTP_STATUS_LENGTH_REQUIRED = 411,
      HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
      HTTP_STATUS_URI_TOO_LONG = 414,
      HTTP_STATUS_HEADER_TOO_LARGE = 431,
      HTTP_STATUS_INTERNAL_ERROR = 500,
      HTTP_STATUS_UNAVAILABLE = 503
  } HttpStatus;
  ```
- **原因**：状态码是协议语义，不是可调参数；enum 比宏更安全，也能和 `StatusText()` 对齐
- **验证**：`AgentSpace/test/src/test_response.c`、`test_integrity.c`、网站集成测试
- **风险**：改动面大，建议机械替换后逐个测试；不要改变数值

### [ ] E-11 `h_size` 的 5 字节余量

- **位置**：`RequestCome/RequestComeMain/RequestComeMain.c:54`
- **现在**：`int h_size=5+(source_end-fd_ob->request_temp_pool->begin+1);`
- **建议**：`CWEB_REQUEST_NODE_EXTRA_BYTES`，默认 5（需确认实际含义：节点/结尾/对齐余量）
- **原因**：这是请求分析块大小的隐藏余量；和 `RECEIVER_QUEUE_ALLOC_BYTES` 的节点布局相关
- **验证**：`test_integrity.c`、QPS；大请求不越界
- **风险**：如果这 5 字节不是固定余量而是某结构体大小，硬编码宏会掩盖真实关系；先确认再改

---

## F. 哈希表容量/负载因子

### [ ] F-01 Router 哈希初始级别

- **位置**：`Router/Router.c:6`
- **现在**：`router->Hrt=RouterHashInit(&e,2);`
- **建议**：`CWEB_ROUTER_HASH_INIT_LEVEL`，默认 2
- **原因**：决定初始桶数（当前 `PRIME_BUCKET_SIZES[1]=17`）；和 A-04 一起改
- **验证**：路由注册、404、路由查找；`AgentSpace/test/src/test_hash.c`
- **风险**：调小会增加链表长度；调大占内存

### [ ] F-02 Router 哈希负载因子

- **位置**：`Router/RouterDataStruct/RouterHash.c:217`
- **现在**：`if(h->elem_num>h->bu_num*2) RouterHashExpend(hm);`
- **建议**：`CWEB_ROUTER_HASH_LOAD_FACTOR`，默认 2；必要时用百分比/浮点宏
- **原因**：决定何时扩容，影响路由查找的链表长度
- **验证**：注册足够多路由后桶数增长；查找正确
- **风险**：调太小频繁扩容；调太大链表变长

### [ ] F-03 Router 哈希表级别上限

- **位置**：`RouterHash.c:6-22`（15 项表）、`:140/144`（`nidex+1`）
- **现在**：没有边界检查；如果扩容次数超过表长，会读越界
- **建议**：
  - 保留表为 `static const int PRIME_BUCKET_SIZES[]`，用 `ARRAY_SIZE`
  - 增加 `CWEB_ROUTER_HASH_MAX_LEVEL`
  - `RouterHashExpend` 在 `nidex + 1 >= ARRAY_SIZE(...)` 时返回 0/-1
- **验证**：构造大量路由触发多次扩容；不崩溃
- **风险**：这是防御性修改，和宏化一起做更安全

### [ ] F-04 Analysiser 哈希初始级别/桶数

- **位置**：
  - `RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.h:20`：`ANALYSISER_HASH_BUCKETS 7`
  - `AnalysiserHash.c:3,182`：`PRIME_BUCKET_SIZES_2[15]`，实际只初始化 6 项
  - `RequestAnalysiser.c:107,137`：`AnalysiserHashInit(h,1)`
- **建议**：
  - `CWEB_ANALYSISER_HASH_INIT_LEVEL`，默认 1
  - `CWEB_ANALYSISER_HASH_INIT_BUCKETS`，默认 7（并让表首项与它一致）
  - 表本身保留 `static const`，用 `ARRAY_SIZE`
- **验证**：`test_parser.c`、query/header 查找
- **风险**：`ANALYSISER_HASH_BUCKETS` 和 `PRIME_BUCKET_SIZES_2[0]` 现在是重复常量，改一个不改另一个会不一致

### [ ] F-05 Analysiser 哈希溢出条目上限

- **位置**：`RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.h:21`
- **现在**：`#define ANALYSISER_HASH_SPILL_MAX 128`
- **建议**：`CWEB_ANALYSISER_HASH_SPILL_MAX`，默认 128；加 `#ifndef`
- **原因**：每个请求的 header/query 条目上限；和 `CWEB_WORKER_HTTP_POOL_BYTES` 联动
- **验证**：大量 header 返回 400；正常 header 通过
- **风险**：调大需要更大的 `http_pool`/分析块，否则提前返回 400

### [ ] F-06 FdConnectTable 初始级别

- **位置**：`FdConnectManage/FdConnectTable/FdConnectTable.c:4`、`Threads/ReceiverThread.c:70`
- **现在**：表 5 项、初始 `4`，当前桶数 59；无扩容
- **建议**：`CWEB_FD_TABLE_INIT_LEVEL`（见 C-10），并加 `CWEB_FD_TABLE_MAX_LEVEL`
- **验证**：连接数增长、连接关闭/重用；`FdTableFind` 正常
- **风险**：见 A-05；这是连接容量的硬限制之一，宏化时要加边界检查

---

## G. 应用/压测入口

### [ ] G-01 默认监听地址/端口

- **位置**：
  - `AgentSpace/main.c:9`：`WebStater("127.0.0.1",5343,...)`
  - `AgentSpace/WebApp/WebApp.c:207`：`/api/info` 里又写死 `5343`
- **现在**：端口 5343 出现两处；如果部署时改端口，`/api/info` 会返回错端口
- **建议**：
  - `CWEB_DEFAULT_HOST`、`CWEB_DEFAULT_PORT`，或启动参数/env
  - `WebApp` 从同一个配置读取端口，不要在 handler 里写死
- **验证**：`/api/info` 返回的 port 与实际监听一致；换端口启动后一致
- **风险**：低；但属于“配置漂移”问题，建议一并修

### [ ] G-02 WebApp 站点根目录/示例大响应

- **位置**：`AgentSpace/WebApp/WebApp.c:26,28`
- **现在**：
  ```c
  #define WEB_ROOT "WebSite"
  #define MEM_BODY_SIZE (256 * 1024)
  ```
- **建议**：这两个已经是宏，保持；可改名为 `CWEB_WEB_ROOT`、`CWEB_WEB_MEM_BODY_BYTES`
- **原因**：这是应用配置，不是框架核心；如果以后多应用，建议放到 app 配置
- **验证**：网站集成测试；`/mem/256k`
- **风险**：低；`WEB_ROOT` 依赖启动目录，宏化不解决 cwd 问题

### [ ] G-03 WebApp 栈缓冲大小

- **位置**：`AgentSpace/WebApp/WebApp.c:155,170-173,191-192,204`
- **现在**：`buf[32]`、`raw/name/msg[256]`、`body[600]`、`esc[512]`、`body[700]`、`body[200]`
- **建议**：应用内宏，例如 `WEBAPP_ECHO_*`；不建议放进框架配置头
- **原因**：这些是示例应用协议/格式限制，不是框架参数
- **验证**：`/echo`、`/api/echo`、`/api/info`；长参数不越界
- **风险**：调大栈占用；调小截断 JSON

### [ ] G-04 bench 入口默认值

- **位置**：`bench/bench_main.c:14-15,32,44`
- **现在**：
  - 慢请求默认 10000us
  - `/mix` 每 10 个请求慢一次
  - 默认端口 15443
- **建议**：`BENCH_SLOW_US_DEFAULT`、`BENCH_MIX_EVERY`、`BENCH_DEFAULT_PORT`；已有 env 的继续保留
- **原因**：压测入口独立，宏化只影响 benchmark，不影响服务
- **验证**：`make qps`；`BENCH_SLOW_US`/`BENCH_PORT` 覆盖生效
- **风险**：低

### [ ] G-05 `bench/run_qps.sh` 的默认值

- **位置**：`bench/run_qps.sh:18-43`
- **现在**：默认时长 2s、连接 300/500、组数按核数、端口 15000+、最大组 32
- **建议**：已经是 shell 变量/环境变量，不需要改成 C 宏；如需统一，可抽出 `bench/config.sh`
- **验证**：`bash bench/run_qps.sh`；`BENCH_GROUPS`/`BENCH_THREADS`/`ROUTES` 覆盖
- **风险**：低

---

## H. 验证矩阵（每批改完照做）

### H-01 编译

```bash
cd /home/yaoxianglongde/c_code/c_web
make clean
make -j"$(nproc)" OPT="-O2 -march=native"
```

要求：无新增 error；警告不要明显变多。

### H-02 单元测试

```bash
bash AgentSpace/test/run_tests.sh
```

需要网站/大文件时：

```bash
bash AgentSpace/test/run_tests.sh --website
```

### H-03 QPS 回归

```bash
make qps OPT="-O2 -march=native"
```

记录三项：`fast` QPS、`slow` P99、`mix` P99。  
纯宏替换（数值不变）应当和改前基本一致；如果明显回退，先查是否误改了值或类型。

### H-04 环境变量/边界

```bash
# 组数边界
CWEB_GROUP_NUM=0 ./main
CWEB_GROUP_NUM=999 ./main

# 并行开关（A-02 修完后）
unset CWEB_PARALLEL; ./main
CWEB_PARALLEL=0 ./main
CWEB_PARALLEL=1 ./main

# worker 数（A-01 修完后）
CWEB_WORKER_NUM=2 ./main
CWEB_WORKER_NUM=8 ./main
```

用启动日志 + `ps -L -p $(pgrep -f './main')` 对照线程数。

### H-05 内存/泄漏（改池参数或 MemoryPool 后）

```bash
valgrind --leak-check=full --log-file=/tmp/cweb_vg.log ./main
# 另开终端压测
wrk -t2 -c100 -d5s http://127.0.0.1:5343/hello
```

检查 `definitely lost`、`invalid read/write`、`uninitialised`。

### H-06 改动审查

```bash
git diff --stat
git diff --check
git status --short
```

要求：没有 core、没有临时文件、没有无意改值。

---

## I. 风险/注意事项

### [ ] I-01 `MemoryPool` 当前不是硬上限

- **位置**：`MemoryPool/MemoryPool.c:32-52`、`:54-88`、`MemoryPool.h:22`
- **现状**：`MemoryPoolInit(perSize, entryNum)` 的 `quotaBytes` 只用于统计；`MemoryPoolAlloc` 实际 `malloc`，只有真 OOM 才 `notfull=0`。
- **影响**：把 `CWEB_RX_*_POOL_BYTES`、`CWEB_WORKER_*_POOL_BYTES` 宏化后，**不会**限制实际内存；不要用这些宏做容量保证。
- **建议**：报告里把池大小宏标注为“配额/统计参数”；如果要做硬上限，先恢复/实现真正的池分配器，再谈宏化。
- **验证**：看 `MemoryPoolStat`；`grep quotaBytes` 确认没有强制检查。

### [ ] I-02 任务队列/暂存环/发送窗口要一起调

- **位置**：`TASK_QUEUE_NUM`、`TEMP_PACK_MAX`、`CWEB_SEND_WINDOW`
- **关系**：
  - 任务队列满 → 请求进 `TempPack`
  - `TempPack` 满 → `ReceiverToWorker` 返回 -1 → 503
  - 发送窗口满 → `pending=1`，请求暂存到下一次 `RequestComeMain`
- **建议**：一次性调大时，算一下每组/每连接的内存和最大积压；不要只调一个。

### [ ] I-03 池大小是“每组/每 worker”乘出来的

- **公式**：
  - 接待线程：`group * (rx_http_pool + rx_store_pool)`
  - worker：`group * workers_per_group * (worker_txt_pool + worker_http_pool)`
- **示例**：3 组 × 4 worker 时，2MB txt + 256KB http 就有约 27MB 的配额参数；32 组会到约 288MB。
- **建议**：宏注释里写清“per group/per worker”，避免误以为是全局总量。

### [ ] I-04 `CWEB_PARALLEL` 的旧语义

- 修改 A-02 后，`CWEB_PARALLEL=0` 会和以前不同；确认部署脚本没有依赖“任意值即开启”。
- 如果不确定，先只把判断改成 `p != NULL && strcmp(p,"0") != 0`，保持其他行为不变。

### [ ] I-05 `ThreadPoolInit` 的下限

- `ThreadPool/ThreadPool.c:41-44`：`totalNum <= 1` 直接返回 0，不创建 worker。
- 如果 A-01 允许 `CWEB_WORKERS_PER_GROUP=1`，要同时修这里，否则服务启动后没有 worker 消费任务。
- 建议：`CWEB_WORKERS_PER_GROUP` 下限设为 2，或让 `ThreadPoolInit` 支持 1。

### [ ] I-06 `MAX_REQUEST_BYTES` 与 body/header 上限的约束

- 建议在配置头里加静态断言或启动检查：
  ```c
  /* 示例：至少能容纳一个最大 body + 基本头部 */
  #if CWEB_MAX_REQUEST_BYTES < CWEB_MAX_BODY_BYTES
  #error "CWEB_MAX_REQUEST_BYTES must be >= CWEB_MAX_BODY_BYTES"
  #endif
  ```
- 否则调大 body 会被总上限提前 413。

### [ ] I-07 哈希表表项数量

- `RouterHash` 的 `PRIME_BUCKET_SIZES` 有 15 项；`AnalysiserHash`/`FdConnectTable` 声明 15 项但只初始化 5/6 项。
- 宏化初始级别后，必须加 `ARRAY_SIZE`/上界检查，否则调到未初始化项会得到 0。
- 不要用宏替换整张表；表保持 `static const int`，只把“初始级别/负载因子/最大级别”做成宏。

### [ ] I-08 类型/溢出

- 字节数宏用 `int` 时，`group * workers * pool_bytes` 可能超过 `INT_MAX`；计算内存时用 `long long`/`size_t`。
- `CWEB_ROUND_UP_KB(n)` 对负数/超大值没有保护；调用前确保 `n >= 0`。
- `snprintf` 的 `size_t` 参数注意格式符。

### [ ] I-09 `#ifndef` 与 `-D`

- 所有想支持 `-D` 覆盖的宏都必须用 `#ifndef` 包住；否则会出现 redefinition 警告/错误。
- 如果保留旧宏别名，建议只保留一段时间，最终统一到 `CWEB_*`，避免两套名字不同步。

---

## J. 推荐逐项修改顺序

下面按“风险从低到高、依赖从少到多”排列。每完成一组，勾掉对应项并跑 H-01/H-02。

### 第 1 批：配置一致性（建议先做）

- [ ] A-01 `CWEB_WORKER_NUM` / `PER_RECEIVER_WORKER` / 日志统一
- [ ] A-02 `CWEB_PARALLEL` 改为布尔解析
- [ ] A-03 `RSP_HEADER_BLOCK` 注释修正
- [ ] A-04 Router 哈希初始级别/扩容级别
- [ ] A-05 FdConnectTable 表项边界检查
- [ ] A-06 删除未使用的 `sleepInit`/`sleepAdd`

> 验证：`make clean && make ...`；启动日志线程数；`AgentSpace/test/run_tests.sh`。

### 第 2 批：只新增配置头，不改行为

- [ ] B-01 新建 `Include/CWEBConfig.h`
- [ ] B-02 增加 `CwebEnvInt` / `CwebEnvBool`
- [ ] B-03 把现有宏逐步指向新名字（可选兼容别名）

> 验证：编译通过；所有行为不变；`CWEB_GROUP_NUM` 边界正常。

### 第 3 批：线程/队列/事件/超时

- [ ] C-01 任务队列容量
- [ ] C-02 暂存环容量
- [ ] C-03 worker 阻塞判定阈值
- [ ] C-04 组数/worker 数默认值与上限
- [ ] C-05 epoll 事件上限
- [ ] C-06 发送批处理上限
- [ ] C-07 worker 空队列等待
- [ ] C-08 队列未绑定休眠
- [ ] C-09 计时器初始容量
- [ ] C-10 连接哈希初始级别
- [ ] C-11 listen backlog
- [ ] C-12 空闲超时
- [ ] C-13 发送窗口

> 验证：H-01/H-02 + H-03 QPS；`CWEB_GROUP_NUM` 1/3/32；`CWEB_PARALLEL` 0/1。

### 第 4 批：内存池/缓冲区

- [ ] D-01 接待线程 HTTP 池
- [ ] D-02 接待线程暂存池
- [ ] D-03 worker 响应池
- [ ] D-04 worker 请求池
- [ ] D-05 每连接初始暂存区
- [ ] D-06 请求暂存 malloc 阈值
- [ ] D-07 响应头块/首行
- [ ] D-08 错误响应缓冲
- [ ] D-09 Content-Length 缓冲
- [ ] D-10 1KB 对齐统一
- [ ] D-11 ThreadPool `/proc` 缓冲
- [ ] D-12 MemoryPool 头部常量

> 验证：H-01/H-02/H-03；大文件 `/download/10mb`；`/mem/256k`；H-05 valgrind。

### 第 5 批：HTTP 限制/协议

- [ ] E-01 总请求上限
- [ ] E-02 URL 上限
- [ ] E-03 header 上限
- [ ] E-04 body 上限
- [ ] E-05 Content-Length 前缀
- [ ] E-06 Content-Length 位数
- [ ] E-07 CRLFCRLF 长度
- [ ] E-08 方法表/方法索引（建议同时修 0/GET 歧义）
- [ ] E-09 方法最小判断字节
- [ ] E-10 HTTP 状态码 enum
- [ ] E-11 `h_size` 的 5 字节余量

> 验证：`test_integrity.c`、`test_parser.c`、`test_response.c`；`--website`；超长 URL/header/body 的 414/431/413。

### 第 6 批：哈希表

- [ ] F-01 Router 初始级别
- [ ] F-02 Router 负载因子
- [ ] F-03 Router 最大级别/越界
- [ ] F-04 Analysiser 初始级别/桶数
- [ ] F-05 Analysiser 溢出上限
- [ ] F-06 FdConnectTable 初始级别/上限

> 验证：`test_hash.c`（如存在）、`test_parser.c`；大量路由/header/连接的压力测试。

### 第 7 批：应用/压测入口

- [ ] G-01 默认 host/port 统一（含 `/api/info`）
- [ ] G-02 WebApp 根目录/示例大响应
- [ ] G-03 WebApp 栈缓冲
- [ ] G-04 bench 默认值
- [ ] G-05 `run_qps.sh` 默认值（可选）

> 验证：`/api/info` 端口正确；`make qps`；网站集成测试。

### 第 8 批：收尾

- [ ] 全量 `make clean && make`
- [ ] `bash AgentSpace/test/run_tests.sh --website`
- [ ] `make qps` 和改前对比
- [ ] valgrind 关键路径
- [ ] `git diff --stat` / `git diff --check` / `git status`
- [ ] 更新本报告勾选状态

---

## K. 快速索引（按文件）

| 文件 | 相关项 |
|---|---|
| `GlobalResource/GlobalResource.h` | A-01, C-01, C-03, C-04 |
| `Threads/ReceiverThread.c` | C-05, C-06, C-09, C-10, D-01, D-02 |
| `Threads/ReceiverThread.h` | C-02 |
| `Threads/WorkerThread.c` | A-03, A-06, C-07, C-08, D-03, D-04, D-07, D-09 |
| `StaterMain/StarterMain.c` | A-01, C-11, C-12, C-13 |
| `ThreadPool/ThreadPool.c` | A-02, D-11, I-05 |
| `RequestCome/RequestComeMain/RequestComeMain.c` | D-05, E-01, E-11 |
| `RequestCome/RequestAnalysis/RequestAnalysiser.c` | E-02, E-03, E-07, D-10 |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | E-04, E-05, E-06, E-07, E-08, E-09 |
| `RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.*` | F-04, F-05 |
| `RequestAndResponse/Response.c` | D-10 |
| `FdConnectResource/RequestTempPool/RequestTempPool.c` | D-06, D-10 |
| `FdConnectResource/SendPointerPool/SendPointerPool.c` | D-10 |
| `FdConnectOrClose/FdConnectOrClose.c` | D-08, E-10 |
| `FdConnectManage/FdConnectTable/FdConnectTable.c` | A-05, F-06 |
| `Router/Router.c` | A-04, F-01 |
| `Router/RouterDataStruct/RouterHash.c` | A-04, F-02, F-03 |
| `MemoryPool/MemoryPool.c` | D-12, I-01 |
| `AgentSpace/main.c` | G-01 |
| `AgentSpace/WebApp/WebApp.c` | G-01, G-02, G-03 |
| `bench/bench_main.c` | G-04 |
| `bench/run_qps.sh` | G-05 |

---

## L. 最后提醒

1. **先修 A 组，再谈宏化**：`CWEB_WORKER_NUM` 现在不生效，`CWEB_PARALLEL=0` 也会开启并行；不先修，配置项的“灵活性”是假的。
2. **纯宏替换不要改值**：先按当前值落宏，跑通测试；下一次 commit 再调值，方便定位回归。
3. **池大小不是硬上限**：当前 `MemoryPool` 是 malloc 过渡实现，`quotaBytes` 只统计；不要用池大小宏做容量承诺。
4. **哈希表宏化要加上界**：`AnalysiserHash`/`FdConnectTable` 的表声明 15 项但只初始化 5/6 项；调到未初始化项会得到 0。
5. **HTTP 方法索引有真实风险**：`h_method=0` 同时表示“未找到”和 GET；`h_method>=7` 不包含 POST；建议用 `enum` 单独修，不要只把 7/8/9 换成宏。

（本报告为只读检查结果，未修改源码。）

