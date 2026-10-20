# 预备工作线程（Standby Worker）待办事务

> 生成日期：2026-10-20  
> 基线版本：`main / eaaca43`，`-O0` 构建  
> 实测环境：12 vCPU / 15GiB，AMD Ryzen 9 8945HX，性能模式  
> 约束：**本文档只记录设计与任务，不修改任何源码。**  
> 相关报告：`AgentSpace/PERF_TEST_REPORT_12C_2026-10-20.md`

---

## 0. 背景

当前 worker 模型（`main / eaaca43`）：

- `StaterMain.c:78-105`：`CWEB_GROUP_NUM` 控制组数，`CWEB_WORKER_NUM` 控制每组 worker 数。
- `StaterMain.c:107` `StartThread()`：每组创建 1 个 `Receiver_Thread`，并为该组创建 `CWEB_WORKER_NUM` 个 worker；所有 worker 绑定同一个 `w->taskQueue`。
- `Threads/WorkerThread.h`：每个 `Worker_Thread` 有独立的 `txt_pool`、`http_pool`、`memory_queue`，但共享本组的 `Task_Queue* taskQueue`。
- `Threads/WorkerThread.c:120` `GetFromTaskQueue()`：队列空时 `sem_timedwait(..., 100ms)`。
- `Threads/WorkerThread.c:154` `WorkerMain()`：取一条任务并完成解析/路由/构造响应。
- `Queue/TaskQueue/TaskQueue.h:18-36`：双缓冲队列；`pending_idx` 原子认领，多消费者安全。
- `Threads/ReceiverThread.c:102` `ReceiverToWorker()`：接待线程把请求 `TasKQueuePush` 到本组队列。

### 0.1 为什么现在要做“预备工作线程”

12 核实测（见测试报告）：

- `G16W1`（16 组，每组 1 接待 + 1 worker）峰值约 **55~56 万 QPS**，是当前拐点；
- `G18/G20` 继续加组反而下降，说明**常驻线程继续增加会过订阅**；
- 2 核时代 `G1W2`（同一队列 2 个 worker）明显低于 `G1W1`，说明**多个 worker 抢同一条队列有原子/信号量竞争**。

因此目标不是“无脑加 worker”，而是：

> **预创建少量 standby worker，平时不参与取任务；当本组任务队列积压/延迟升高时快速激活，峰值过后自动回落到待命状态。**

这样可以在不永久增加线程和调度压力的前提下，吸收突发流量。

### 0.2 本文对“预备工作线程”的定义（先对齐语义）

本文默认指：

1. **启动时就创建**，不是运行中 `pthread_create`；
2. 已绑定到本组 `Task_Queue` 和本组 `Receiver_Thread`；
3. 默认处于 **parked / standby** 状态，不调用 `TasKQueueTopAndPop`；
4. 达到激活条件后变成 **active**，与基础 worker 一起消费本组任务；
5. 队列回落到低水位并持续空闲后，重新 park；
6. **至少保留一个基础 active worker**，不能全部 park。

如果“预备工作线程”在你的语境里是“主 worker 崩溃/退出后的替补”，本文的设计仍然适用，但需要额外增加 worker 健康检查与替换逻辑（见 §7 开放问题）。

---

## 1. 现状约束与已知事实

| 事实 | 影响 |
|---|---|
| `Task_Queue` 已支持多消费者（`pending_idx` 原子自增） | standby 可以直接复用现有 `TasKQueueTopAndPop`，不需要重写队列 |
| `Task_Queue` 的 `num`、`pending_idx`、`pending_reader`、`swapping` 等字段在同一结构体内 | 多 worker 高并发时存在 cacheline/原子竞争；standby 必须默认不参与，否则会放大竞争 |
| 每个 worker 有独立 `txt_pool/http_pool/memory_queue` | standby 必须各自初始化自己的池，不能共享 |
| 响应顺序由 `serial` + `ReceiverQueue` 保证 | 增加/减少消费者不破坏同连接有序发送 |
| `ReceiverQueuePush` 仍是阻塞式 `sem_wait` | 如果瓶颈在“接待线程发送”，standby 帮不上；需要监控响应队列 |
| `GetFromTaskQueue` 当前固定阻塞 100ms | standby 不能直接复用这个阻塞循环，否则无法及时响应 park 指令 |
| `Receiver_Thread` 已指向本组 `taskQueue` | 激活入口可以放在 `ReceiverToWorker()` 或 push 之后 |

### 1.1 2 核/12 核的既有实测结论

| 配置 | 结果 | 结论 |
|---|---|---|
| 2 核 `G1W1` vs `G1W2` | 约 7.1 万 vs 6.0 万 QPS | 同一队列加 worker 会变慢 |
| 2 核 `G2W1` | 约 10 万 QPS | 独立队列/接待线程更有效 |
| 12 核 `G16W1` | 约 55~56 万 QPS | 组数扩展有效 |
| 12 核 `G18/G20` | 下降到约 45~54 万 | 常驻线程过订阅，继续加组无收益 |

> 这决定了 standby 的验收标准：**只有在突发场景降低 p99/队列等待，且稳态不退化时才保留。**

---

## 2. 推荐方案（MVP）

### 2.1 方案 A：每组“基础 worker + 少量 standby”

**推荐先做这个。**

- `CWEB_WORKER_NUM` 保持现有语义：每组基础 active worker 数（默认 1）。
- 新增 `CWEB_STANDBY_WORKER_NUM`：每组额外 standby 数（默认 **0**，功能关闭）。
- 新增可选参数：
  - `CWEB_STANDBY_HIGH`：队列高水位，默认 32；
  - `CWEB_STANDBY_LOW`：队列低水位，默认 8；
  - `CWEB_STANDBY_IDLE_MS`：低水位持续多久后 park，默认 50ms。
- 启动时创建 `base + standby` 个 worker，全部绑定本组 `taskQueue`；standby 进入 park 循环。
- 激活入口：`ReceiverToWorker()` push 成功后读取 `taskQueue->num`；达到 high 时唤醒一个 standby。
- 回落入口：active standby 在完成当前任务后，若 `num <= low` 且持续 `idle_ms`，尝试 park；`active_count <= base` 时禁止再 park。

### 2.2 为什么不用“全局 standby 池”

全局池需要跨组借调 worker，涉及临时绑定队列、跨组内存池归属、归还/抢占竞态，复杂度明显高于收益。**第一版不做**。

### 2.3 为什么不做“每请求创建线程”

线程创建/销毁成本远高于任务本身，且会引入不可控的线程数；standby 必须是**预创建**的。

## 3. 数据结构建议（仅设计，不改代码）

> 下面只是文档中的目标结构，不是已经落地的代码。

```c
typedef struct Worker_Group {
    Task_Queue* q;              /* 本组任务队列，来自 ReceiverThread */
    Worker_Thread* workers;     /* base + standby 数组 */
    int total;                  /* base + standby */
    int base;                   /* 至少保留的 active 数 */
    atomic_int active;          /* 当前 active 数 */
    pthread_mutex_t mu;         /* 保护 active/state 迁移 */
    sem_t wake;                 /* 唤醒一个 standby */
    int high, low, idle_ms;
    atomic_ullong stat_wake;    /* 激活次数 */
    atomic_ullong stat_park;    /* park 次数 */
} Worker_Group;

typedef struct Worker_Runtime {
    Worker_Thread* worker;
    Worker_Group* group;
    int is_standby;
    atomic_int active;
} Worker_Runtime;
```

### 3.1 状态机

```text
STANDBY --wake/claim--> ACTIVE --idle/low & active>base--> STANDBY
   ^                         |
   +------ 不满足条件 <-------+
```

规则：

1. `STANDBY` 不调用 `TasKQueueTopAndPop`；
2. `ACTIVE` 才能取任务；
3. 当前任务必须完成后才能 park；
4. `active <= base` 时禁止 park，保证基础消费能力；
5. 激活/回落都用原子 CAS + 锁，避免两个线程同时改 `active`。

### 3.2 激活逻辑（伪代码）

```c
/* ReceiverToWorker 成功 push 之后 */
if (g->standby_total > 0 && TasKQueueDepth(g->q) >= g->high) {
    WorkerGroupWakeOne(g);   /* CAS active < total，再 sem_post(&g->wake) */
}
```

`WorkerGroupWakeOne` 必须做到：

- 只有一个唤醒者成功（CAS）；
- 不重复 `sem_post` 唤醒多个 standby（除非积压继续升高）；
- standby 数为 0 时完全零额外开销（分支预测友好）。

### 3.3 standby 循环（伪代码）

```c
void WorkerRuntimeMain(Worker_Runtime* rt) {
    while (1) {
        if (rt->is_standby && !atomic_load(&rt->active)) {
            sem_wait(&rt->group->wake);
            atomic_store(&rt->active, 1);
        }

        Task_Queue_Entry t;
        int e = 0;
        t = TasKQueueTopAndPop(rt->group->q, &e);

        if (e == 1) {
            WorkerMainOnce(rt->worker, &t);   /* 需要从 WorkerMain 拆出单次处理 */
            continue;
        }

        /* 空队列：判断是否回落到 standby */
        if (rt->is_standby &&
            TasKQueueDepth(rt->group->q) <= rt->group->low &&
            atomic_load(&rt->group->active) > rt->group->base &&
            IdleLongEnough(rt->group->idle_ms)) {
            if (WorkerGroupTryPark(rt->group)) {
                atomic_store(&rt->active, 0);
                continue;
            }
        }

        /* 活跃 worker 等待新任务；超时用于检查 park 条件 */
        sem_timedwait(&rt->group->q->sem_task_notempty, &short_deadline);
    }
}
```

> 关键重构：当前 `WorkerMain()` 内部会先 `GetFromTaskQueue()`；实现 standby 时需要把它拆成
> `WorkerMainOnce(worker, task)` + 独立的取任务循环，避免 standby 被 100ms 阻塞卡住。

---

## 4. 任务拆解（TODO）

| ID | 优先级 | 任务 | 主要文件/位置 | 验收 |
|---|---|---|---|---|
| S-01 | P0 | 新增 `CWEB_STANDBY_WORKER_NUM` / high/low/idle 配置解析；默认 standby=0 | `StaterMain/StarterMain.c:78-105` | 启动日志打印 base/standby；不设环境变量时行为与现在完全一致 |
| S-02 | P0 | 新增 `Worker_Group` / `Worker_Runtime` 结构；初始化互斥量、信号量、计数器 | 新文件或 `Threads/WorkerThread.h` | 编译通过；init/destroy 路径完整 |
| S-03 | P0 | `StartThread()` 创建 base+standby，全部绑定本组 `w->taskQueue`；worker id 唯一 | `StaterMain/StarterMain.c:107+` | G16W1+S1 启动 49 线程；线程名可区分 |
| S-04 | P1 | 拆分 `WorkerMain()`：提取 `WorkerMainOnce(worker, entry)`；保留原行为给 active worker | `Threads/WorkerThread.c:154` | 单测/压测无功能回归 |
| S-05 | P1 | 实现 standby park/wake 循环；只在 active 时调用 `TasKQueueTopAndPop` | `Threads/WorkerThread.c` | standby 空闲时不占 CPU；无忙等 |
| S-06 | P1 | 在 `ReceiverToWorker()` push 成功后按高水位唤醒一个 standby | `Threads/ReceiverThread.c:102` | 高积压时 active 数上升；低积压回落 |
| S-07 | P1 | 回落逻辑：低水位 + idle_ms + 保留 base；防止全部 park | `Threads/WorkerThread.c` | 长跑后至少 1 个 active；无死锁 |
| S-08 | P1 | 增加队列深度访问器（避免直接读内部字段） | `Queue/TaskQueue/TaskQueue.h/.c` | 原子读；不引入锁竞争 |
| S-09 | P2 | 统计：wake/park 次数、最大队列深度、平均 active；启动日志 | `Worker_Group` / `StarterMain.c` | 可从日志/接口观测 |
| S-10 | P2 | 更新 README 配置表、TODO 文档、测试脚本 | `README.md`、`AgentSpace/` | 文档与实现一致 |

---

## 5. 正确性与竞态检查清单

- [ ] `Task_Queue` 多消费者：同一条任务只被一个 worker 认领（`pending_idx` 原子）。
- [ ] standby 不参与取任务时，不改变 `sem_task_notempty` 的既有语义。
- [ ] wake 使用 CAS + `sem_post`，避免多个 standby 同时被唤醒（thundering herd）。
- [ ] 回落时先完成当前任务，再 park；不能把进行中的任务丢在半路。
- [ ] `active` 计数与 `base` 判断在锁内/原子操作内完成，避免全部 park。
- [ ] 每个 worker 的 `txt_pool/http_pool/memory_queue` 独立；standby 不能复用别人的池。
- [ ] `ReceiverQueue` 多生产者/有序发送不受影响；20 条流水线保序。
- [ ] `Task_Queue` 满时 `TasKQueuePush` 仍非阻塞返回 0；不能因为 standby 改变背压语义。
- [ ] 进程退出/信号路径不产生悬挂线程（当前服务长期运行，重点是运行期稳定）。

## 6. 验收标准

### 6.1 功能

- 0 丢失/重复请求；同连接 20 条流水线顺序正确；
- 10MB 下载 sha256 与源文件一致；
- c400/c1200 下 0 个 503；
- 压测后 `/hello` 200，RSS/fd 稳定；
- 连续 10 分钟混合负载无死锁、无线程数增长。

### 6.2 性能

以当前 `G16W1` 为基线（12 核，c1200，`wrk -t10`，约 54.6~55.9 万）：

| 指标 | 目标 |
|---|---|
| 稳态 QPS | 不低于基线 2% |
| 突发/高并发 p99 | 改善 ≥5% 或队列等待下降 |
| 空闲 CPU | 不高于基线 +1%（standby 必须真正 park） |
| 503 | 0 |
| 线程数 | 启动后恒定，不随请求增长 |

### 6.3 测试矩阵

- 组数：`G1 / G2 / G4 / G8 / G16`
- standby：`0 / 1 / 2`
- 并发：`c50 / c200 / c800 / c1200`
- 客户端：`wrk -t6`、`-t10`
- 环境：12 核优先；至少在 2 核上回归一次 `G1/G2`
- 观测：sleep 期间 standby CPU 必须接近 0

---

## 7. 风险、回退与开放问题

### 风险

1. **共享队列竞争**：standby 激活后仍然是同一 `Task_Queue` 的多消费者；可能像 `G1W2` 一样变慢。
2. **过订阅**：12 核上 `G16W1` 已有 33 线程；再加 standby 可能超过拐点。
3. **唤醒抖动**：高/低水位设置不当会造成频繁 wake/park。
4. **内存增加**：每个 worker 有独立内存池；standby 也会预占内存。
5. **观测不足**：没有 wake/park/active 统计就无法判断是否值得保留。

### 回退

- 默认 `CWEB_STANDBY_WORKER_NUM=0`，功能关闭；
- 若稳态 QPS 下降或 p99 退化，直接保持 0，不合并为默认；
- 代码层保留开关，不做“自动默认开启”。

### 开放问题

1. “预备工作线程”是 **parked 备用**，还是 **主 worker 故障后的替补**？后者需要健康检查/重启逻辑。
2. 激活指标用队列深度、排队时间，还是接待线程响应延迟？
3. 是否允许跨组借调（全局池）？第一版建议不允许。
4. standby 是否需要在 `/api/info` 暴露状态？当前不改 WebApp，先写日志/统计。
5. 是否把 `CWEB_WORKER_NUM` 重新定义为“总 worker 数”，还是保持“基础 active 数”+ 新增 standby？本文建议后者，避免破坏现有行为。

---

## 8. 建议的落地顺序

1. 先做 **S-01/S-02/S-03**，保证 standby=0 时行为完全不变；
2. 再做 **S-04/S-05/S-08**，把 worker 单次处理与 park 循环拆开；
3. 加 **S-06/S-07**，用小流量验证激活/回落；
4. 跑 §6.3 测试矩阵，先看 `G16W1 + standby=1`，对比基线；
5. 只有在突发场景有明确收益时，才考虑默认开启或调大 standby；
6. 最后补 **S-09/S-10** 和 README。

---

## 9. 参考

- `StaterMain/StarterMain.c:78-105`：组数/worker 数配置
- `StaterMain/StarterMain.c:107+`：`StartThread` 创建接待与 worker
- `Threads/WorkerThread.c:86`：`WorkerThreadInit`
- `Threads/WorkerThread.c:120`：`GetFromTaskQueue`（100ms 阻塞）
- `Threads/WorkerThread.c:154`：`WorkerMain`
- `Queue/TaskQueue/TaskQueue.h:18-36`：双缓冲多消费者队列
- `Queue/TaskQueue/TaskQueue.c:53`：`TasKQueuePush`
- `Queue/TaskQueue/TaskQueue.c:82`：`TasKQueueTopAndPop`
- `Threads/ReceiverThread.c:102`：`ReceiverToWorker`
- `AgentSpace/PERF_TEST_REPORT_12C_2026-10-20.md`：12 核实测数据


