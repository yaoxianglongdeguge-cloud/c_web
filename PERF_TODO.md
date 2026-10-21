# c_web 性能优化待办

> 记录时间：2026-10-21
> 当前配置：`CWEB_EPOLL_TICK_MS=2`、`ONE_TASK_TIME=4`（4ms 超时）、`-O2 -march=native`
> 原则：**不改收发线程架构**；只做组件级优化。目标 fast 从 516k 提到 700k~800k（6 服务核）。

---

## 0. 当前基线（同机 12 核，服务端 6 核 / wrk 6 核，`-t6`，2s）

| 场景 | QPS | 延迟 | 配置 |
|---|---:|---:|---|
| fast | **516,362** | 1.00ms | g5, c500 |
| 阻塞 10ms | 193（g1/c50）→ **6,158**（g32/c500） | 78ms | 靠组数 |
| 混合 10ms（1/10 慢） | 2,070（g1/c50）→ **61,485**（g32/c500） | 8.2ms | 慢请求容量决定 |
| CPU 密集 50ms | 19.9（g1，不换主） | 1.0s | `state=='R'` 保护 |
| 阻塞 50ms | 77.6（g1，单 worker 基线 19.9） | 527ms | 约 3.9× |

> 同机 `wrk`、2s、±10%；fast 可能被客户端限制，最终对比建议用独立客户端 + 10~30s + P99/RSS/fd。

---

## 1. P0：先做，收益最直接

### P0-1 `TimerReset` O(1) 化
- **问题**：`TimerReset` / `TimerAlloc` / `TimerFree` 都是线性扫描 `queue[1..elem_end]` 找 fd；每个事件/响应都调用一次，连接多时是纯 CPU 浪费。
- **改法**：
  - 在 `Timer` 或 `Prior_Queue_Timer` 里加 `int* pos`（fd → heap index），`pos[fd] = idx`；
  - `PriorQueueTimerSwap` 时同步交换 `pos`；`Insert/Pop/TimerFree/TimerExpend` 时维护；
  - `TimerReset(fd)` 直接 `pos[fd]`，O(1)；找不到再回退线性扫描（兼容）。
  - 也可以把 timer index 存进 `Fd_Entry`，避免单独数组。
- **文件**：`FdConnectManage/Timer/Timer.c`、`TimerDataStruct/PriorQueueTimer.c/.h`、`FdConnectTable`（可选）。
- **验收**：`test_timer` 36 PASS；c500 下 QPS 有可测提升；超时行为不变。
- **风险**：`TimerExpend`/`Pop` 后 index 失效；加 assert/单测覆盖。

### P0-2 内存池：减少每请求 malloc/free
- **问题**：`MemoryPool` 现在是 malloc 薄封装（32B 头 + 原子统计），每请求约 4~5 次分配/释放：请求副本、解析块、4KB 头块、响应打包块、Response body。
- **改法（二选一）**：
  - A. 每线程尺寸类 free list：1KB/2KB/4KB/8KB，`MemoryPoolAlloc` 取对应类，`Free` 按头部 size 回链；>8KB 走 malloc；不做跨线程 free。
  - B. 恢复 `#if 0` 的位图池（先修跨尾误判/非扩容语义），但改动更大。
- **文件**：`MemoryPool/MemoryPool.c/.h`、调用点无需改（API 不变）。
- **验收**：`test_memory_pool` 25 PASS + valgrind 0 error；QPS 提升；RSS 不涨。
- **风险**：double-free/所有权；保留 magic/owner 调试；不要跨线程 free。

### P0-3 TaskQueue 降同步开销
- **问题**：`TasKQueuePush` 每任务 `mutex + sem_post`；`TasKQueueTopAndPop` 每任务 CAS + `sem_trywait`；双缓冲 swap 还要竞争 `mutex_task`。
- **先测量**：`strace -c -f -p <pid>` 看 `futex` 占比；`perf stat`/`perf top` 看 `pthread_mutex_lock`、`sem_*` 热点。**别盲改。**
- **改法（按收益/风险排序）**：
  - A. 保留双缓冲，只把"每任务 sem"改成"空/非空状态变化时才唤醒"（eventfd/cond），pop 批量取；风险最低。
  - B. MPSC 有界环（单生产者 receiver、多消费者 worker），每槽 sequence + CAS 认领；避免 mutex，但要处理 memory order/ABA/满队列。
  - C. 每 worker 一条 SPSC 队列 + receiver 按 least-loaded/round-robin 派发；消除多消费者竞争，但要加负载均衡和"worker 卡住"处理。
- **推荐**：先 A，再 B；C 等有真实压测需求再做。
- **文件**：`Queue/TaskQueue/TaskQueue.c/.h`。
- **验收**：`test_taskqueue_race` 23 PASS；多生产者/多消费者压力 0 丢任务；QPS 提升；无 lost wakeup。
- **风险**：sem 只用于唤醒、不用于计数；注意 `pending_idx` 溢出和 swap 窗口。

### P0-4 ReceiverQueue：去掉阻塞式 push
- **问题**：`ReceiverQueuePush` 每响应 `sem_wait(sem_thing_queue_notfull)`（容量 100，阻塞）+ `mutex_thing`；worker 可能在响应队列满时永久阻塞，和接收线程形成背压/死锁风险。
- **改法**：
  - 优先：MPSC 有界环（多 worker → 单 receiver），满时返回失败，由上层 503/丢弃或短暂重试；
  - 或：`sem_timedwait` + 超时降级；批量 `TopAndPop`（一次摘整批，减少锁）；
  - 或：每 worker 一条 SPSC 队列，receiver 轮询 drain。
- **文件**：`Queue/ReceiverQueue/ReceiverQueue.c/.h`。
- **验收**：多 worker 压测无死锁；`Thing_queue` 满时行为明确（503/丢弃计数）；QPS 提升。
- **风险**：响应丢失/乱序；必须保留 serial 保序语义；改动前先补测试。

### P0-5 WorkerQueue：响应内存归还
- **问题**：`WorkerQueue` 是带 mutex 的侵入式链表；每个响应 push 一次锁，worker 一次摘链（已经是批量）。
- **改法**：per-worker SPSC 无锁环（receiver → 该 worker），或 lock-free 侵入式栈；保留"批量摘链"。
- **文件**：`Queue/WorkerQueue/WorkerQueue.c/.h`。
- **验收**：valgrind 无泄漏；高并发 RSS 稳定；QPS 小幅提升。
- **风险**：节点就在响应块内，释放顺序不能变（先取 next 再还块）。

---

## 2. P1：有余力再做

### P1-6 发送批量化
- 现在每个响应一次 `write`（头+体已打包成一个块）；文件走 `sendfile`。
- 可做：同一 epoll 批次内 `sendmmsg` 多连接发送；或每连接 `writev`（当前已单块，收益小）。
- **注意**：部分写/EAGAIN/EPOLLOUT 续写、per-connection 顺序不能破坏。
- **验收**：fast QPS +5~10%；`test_server` 27 项、10MB 下载 sha256 不变。

### P1-7 `ThreadPoolChange` 冷却/最短驻留
- 现在无冷却，2ms tick + 4ms 超时会频繁换主；g32 时切换很密。
- 加：
  ```c
  uint64_t last_change_ns;
  if (now - last_change_ns < CHANGE_COOLDOWN_NS) return;
  if (now - last_change_ns < MIN_RESIDENCY_NS) return;
  ```
- 建议 50~200ms 冷却、500ms~1s 驻留；用压测确认吞吐只降 <5%。
- 同时加计数：换主次数、blocked worker 数、无备胎次数。

### P1-8 timerfd 替代 tick 轮询
- 现在靠 `epoll_wait(..., CWEB_EPOLL_TICK_MS=2)` 轮询任务超时；fast 时多 500 次/秒/组唤醒。
- 改：`timerfd_create` + `epoll_ctl`，按 `taskStart + 4ms` 设下一次到期；`epoll_wait(-1)`。
- 收益：fast 回到 tick10 的 ~527k；阻塞仍能在 4ms 精确检查；空闲 CPU 更低。
- **验收**：fast 不降；slow/mix 不降；空闲 CPU 下降。

### P1-9 `PER_RECEIVER_WORKER` / 组数可配置
- 当前 `#define PER_RECEIVER_WORKER 4` 写死；`CWEB_GROUP_NUM` 可调（max 32）。
- 建议：`PER_RECEIVER_WORKER` 也读环境变量；fast 用 1 主 + 3 备即可，阻塞可试 8。
- 记录最优：fast g5；阻塞/混合 g32；32 组 = 160 线程，注意 RSS/调度。
- **验收**：同一压测下给出 4/8 worker × 组数矩阵，选吞吐/资源最优。

### P1-10 线程绑核（可选）
- 每组 receiver/worker 用 `pthread_setaffinity_np` 绑到固定核，减少迁移/缓存失效。
- 注意：`SO_REUSEPORT` 多组 + 客户端同机时，绑核可能反而限制；用 taskset 先验证。
- **验收**：fast/slow/mix 各跑 3 次，QPS 中位数提升或 P99 下降。

## 3. P2：工程化 / 可观测性

### P2-11 指标与日志
- 现在换主、背压、503、队列深度都不可见，出问题只能猜。
- 建议加轻量计数（原子变量即可）：
  - `task_queue_len`、`receiver_queue_len`、`tempPack.count`；
  - `threadpool_changes`、`blocked_workers`、`no_standby_count`；
  - `503_count`、`408_count`、`pool_alloc_fail`；
  - `timer_heap_size`、`fd_count`。
- 可以每 N 秒打印一行，或 `/metrics` 文本接口（不用上 Prometheus）。

### P2-12 API 契约 / 生命周期
- 在 `Handler` 和 `Response` 定义处写死：
  ```c
  // Handler 必须同步完成：返回前写完 Response；
  // 不得把 Request*/Response* 带到其他线程；
  // 业务线程池必须 wait-style 使用；fire-and-forget 未支持（UB）。
  ```
- 可选 debug 哨兵：`Response.magic + in_handler`，`ResponseBodySet/HeaderSet` 里 assert；Release 零开销。
- 如果以后要支持 fire-and-forget，再做 `ResponseDefer/ResponseComplete`（响应/请求生命周期延长），不要现在硬上。

### P2-13 初始化/退出健壮性
- `ThreadPoolInit` 检查 `malloc` / `WorkerThreadInit` / `pthread_create` 返回值；
- `pthread_create` 的 `pthread_t` 保存并 `pthread_detach`（或统一 join）；
- 增加 `ThreadPoolDestroy`（服务退出/测试）；
- `totalNum<=1` 时打印/明确拒绝，不要静默 NULL。

### P2-14 仓库卫生
- 目前 `.o`、`main`、`.d`、`AgentSpace/test/build/` 仍被跟踪/污染；建议：
  ```bash
  printf '*.o\n*.d\nmain\nlibcweb.a\nAgentSpace/test/build/\n' > .gitignore
  git rm -r --cached $(git ls-files '*.o') main AgentSpace/test/build 2>/dev/null
  ```
- README 里的目录/路径与实际（`AgentSpace/main.c`、`AgentSpace/test/`）不一致，顺手对齐；
- 加 LICENSE、CI（build + test + valgrind）。

### P2-15 压测脚本入仓
- 目前 bench 在 `/tmp`：`bench_main.c`、`bench_main50.c`、`run_bin.sh`；
- 建议移到仓库 `bench/`，加入：
  - `bench_main.c`（/fast /slow /mix）
  - `bench_main50.c`（/slow50 /mix50 /cpu50）
  - `run.sh`（setsid + taskset + wrk + 结果解析）
  - `RESULTS.md`（每次改动记录）
- 这样以后优化可以一键复现，不用重建临时文件。

---

## 4. 暂不做（明确边界）

1. **不改"单 receiver + 单发送"架构**：目标 ≤50 万 QPS/6 核，当前 516k 够用；再往上才拆发送/多 receiver。
2. **不为了 fast 加 worker/组**：fast g5 是甜点，g6 已降到 406k；`CWEB_PARALLEL=1` 实测 113k，明确不用。
3. **不支持 fire-and-forget**：契约是 handler 同步完成；业务池 wait-style；异步留给以后的 `ResponseDefer`。
4. **不为 10ms 阻塞追求 4×**：10ms 只有 ~2×（任务太短，重叠窗口小）；50ms 才有 3.9×，这是物理限制。
5. **不拿同机 wrk 的绝对值当生产能力**：客户端 6 线程也接近打满；要绝对上限需独立客户端。

---

## 5. 压测口径（每次改动后照这个跑）

```bash
cd /home/yaoxianglongde/c_code/c_web

# 1) 构建
make clean && make -j4 OPT="-O2 -march=native"

# 2) 启动（服务端 6 核）
setsid taskset -c 0-5 env CWEB_GROUP_NUM=5 ./main >/tmp/srv.log 2>&1 < /dev/null &

# 3) 压测（客户端 6 核）
taskset -c 6-11 wrk -t6 -c500 -d2s --latency http://127.0.0.1:5343/fast

# 4) 记录：QPS / Latency(P50/P99) / Non-2xx / Socket errors / RSS / fd
```

- 每个场景跑 3 次取中位数；
- 对比 fast / slow10 / mix10 三组，组数用 g1 与 g32 各一次；
- 用 `strace -c -f -p <pid>` 看 futex/write/clock 占比，用 `pidstat -t` 看每线程 CPU；
- 快速定位：如果 receiver 线程 100%、worker 低 → 发送/接收瓶颈；如果 worker 高 → 解析/内存/队列。

---

## 6. 优化记录表（每完成一项填一行）

| 日期 | 改动 | fast（g5,c500） | slow10（g1/g32） | mix10（g1/g32） | 备注 |
|---|---|---:|---:|---:|---|
| 2026-10-21 | 基线（tick2/4ms/O2） | 516,362 | 193 / 6,158 | 2,070 / 61,485 | 6+6 核，2s |
|  |  |  |  |  |  |
|  |  |  |  |  |  |

---

## 7. 建议顺序

1. **P0-1 Timer O(1)** → 测 fast；
2. **P0-2 内存池** → 测 fast + RSS；
3. **P0-3 TaskQueue**（先 A 方案）→ 测 fast + `test_taskqueue_race`；
4. **P0-4 ReceiverQueue 非阻塞** → 测 slow/mix + 稳定性；
5. **P0-5 WorkerQueue** → 测 RSS/valgrind；
6. P1-7 冷却 + P1-8 timerfd → 测 fast 与 slow/mix；
7. 其余按需。

> 每做完一项，单独 commit，并填上面的记录表；不要一次改多个，否则 QPS 变化无法归因。


