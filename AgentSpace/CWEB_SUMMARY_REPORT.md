# c_web 审计与优化总结报告

生成日期：**2026-10-15**　代码基线：当前仓库（`/home/yaoxianglongde/c_code/c_web`）
本报告由 Cline 编写，**未改动任何源文件**——所有源码改动均由你本人完成，我负责定位、给出改法、在副本上验证。

---

## 0. 摘要（一页读懂）

**起点**（仓库原始 `./main`）：RST/断连风暴下一轮内 **段错误**；空闲后单请求 **50.7 ms**；`wrk -c50` 只有 **千级** req/s；空闲 CPU **190~195%**（2 核）。

**现在**（当前源码全量重编副本实测）：风暴 **存活**；单请求 **1.4~1.9 ms**；`c50 34,413` / `c200 56,682` req/s；空闲 CPU **1%**；RSS **3300 KB 零增长**；`website_test 27 PASS`。

**共定位 5 类问题、落地 4 组改动**（全部经隔离实验 + A/B 验证）：

| # | 问题 | 一句话根因 | 状态 |
|---|---|---|---|
| D-33~36 | RST 风暴下 SIGSEGV | 4 处 `FdTableFind` 丢弃返回值后解引用 NULL `fd_ob` | ✅ 已修（3 处 + 你已修 1 处） |
| D-38 | 单请求 50.7ms / c50 千级 | `Threads/ReceiverThread.c:66` `epoll_wait(...,50)`；Worker 产出无唤醒通道 | ✅ 已修（改为 1ms） |
| D-39 | keep-alive 上每个静态文件 +42ms | `write(头)`+`sendfile(体)` 两个小段触发 **Nagle × 延迟 ACK**；缺 `TCP_NODELAY` | ✅ 已修（2 文件 3 行） |
| D-40 | 空闲 CPU 190~195% | Worker 空轮询忙等；早期 `tv_nsec` 递增版还会在累计空闲后 `EINVAL` 反向变忙等 | ✅ 已修（起点 0 / 封顶 1ms） |
| — | sem 唤醒升级 | 可把空闲 CPU 压到 **0%**，配 `epoll=1` 时曾测到 `c50 51,812` | ⏸ 方案存档，未采纳 |

**未做（按你的要求暂缓）**：内存池重构、sem 唤醒。

---

## 1. 背景、约束与方法学

**目标**：审计这台纯 C 并发 HTTP 服务器在压力/异常场景下的正确性与性能，定位崩溃与长尾根因，并给出可验证的修复。

**约束**：不改动你的源文件 ⇒ 我的一切验证都在 `/tmp` 的**仓库副本**上进行（`/tmp/base_now`、`/tmp/verify_v2…v9`），源码改动一律交给你手动完成；测试脚本按 **PID 精确清理**，不使用 `pkill -x main`。

**环境**：2 核 VM（`nproc=2`，与 VS Code Server 共存）；`wrk` 压测客户端与服务端**同机**；服务端口 5343（`WebSite/static/10mb.bin` 为 10MiB 测试文件）。

**测试手段**

| 工具 | 用途 |
|---|---|
| `/tmp/audit/rst.py <port> <rounds> <per>` | RST 风暴：连接后发 `GET /hello`(读32B) 或 `GET /download/10mb`(读64B)，再 `SO_LINGER{1,0}` 关闭制造 RST |
| `~/cweb_audit_backup/storm2.sh <树> <标签> [轮数] [每次]` | 安全版一键：启动→空闲CPU→空闲后延迟→c50/c200→RST 风暴→存活/RSS，**只 kill 自己启动的 PID** |
| `test/website_test.sh` | 网站功能集成测试（27 项，含 10MB sha256 逐字节一致、4 路并发） |
| `test/run_tests.sh` | 单元/并发测试（含 `taskqueue_race` 23 项） |
| `~/cweb_audit_backup/keepalive_latency_test.py` | 一条 keep-alive 连接上的逐请求耗时（定位 Nagle 问题） |
| `keepalive_latency_test_quickack.py` | 对照组：客户端每次 `recv` 前置 `TCP_QUICKACK`（验证"是不是延迟 ACK"） |
| `strace -f -tt -T` + `/tmp/audit/gap.py` | 抓 syscall 时间戳并对齐空档，判断延迟发生在**内核**还是**代码** |

**数据可信度声明**

- c50/c200 跨批次波动约 **±30%**，所有关键结论都有**同批对照**；绝对值仅适合同机相对比较。
- "崩溃"以 shell 报 **段错误（核心已转储）** 与 `/proc/<pid>` 消失为准，非推测。
- 延迟测量含同机 `wrk`/`curl` 的调度干扰，故同时使用"顺序 curl（新连接）"与"单连接逐请求"两种口径交叉验证。

## 2. 问题清单（含证据与状态）

| ID | 现象 | 根因 | 关键证据 | 状态 |
|---|---|---|---|---|
| **D-33~36** | RST/断连风暴下 **SIGSEGV**，进程死亡 | 4 处 `FdTableFind` 丢弃/误用返回值后解引用可为 NULL 的 `fd_ob` | 未修版风暴 round 1 后即 `段错误（核心已转储）`；修后 6×80 全身而退 | ✅ 修复 |
| **D-38** | 空闲后单请求 **50.7 ms**；`c50` 仅 1,657 | `Threads/ReceiverThread.c:66` `epoll_wait(...,50)` 是**唯一**投递 Worker 产出的入口，而 Worker 产出**没有任何唤醒**（`sem_thing_queue_notempty` 在 `Include/Variate.h:50` 是注释状态）；连接注册位仅 `EPOLLIN` | 只改超时值（两版都是忙等、同批）：延迟 51ms→**1.4ms**，c50 9,048→**31,051** | ✅ 修复（1ms） |
| **D-39** | keep-alive 上**每个静态文件请求 +42 ms**（首页/CSS/JS/图标），首页 c10 只有 234 req/s | 静态响应被拆成 `write(81B 头)` + `sendfile(3136B 体)` **两个小段** ⇒ 第 2 段被 **Nagle** 扣住等 ACK，客户端空闲时**延迟 ACK**（`TCP_DELACK_MAX=HZ/25=40ms`）⇒ 互等 40ms。全仓库无 `TCP_NODELAY`/`TCP_CORK` | strace：26s 日志里 2 次 write + 2 次 sendfile 相隔 **1.7ms**、15,635 次 `epoll_wait` 空转；客户端置 `TCP_QUICKACK` 后 42ms→**1.7ms** | ✅ 修复（`TCP_NODELAY`） |
| **D-40** | 空闲 CPU **190~195%** | Worker 空轮询忙等；且早期 `tv_nsec` 递增实现会在累计空闲约 85 分钟后**越界成 `EINVAL`**、反向退化成永久忙等 | 隔离实验（忙等 vs 不同睡眠档位）+ `nano_sim` 状态机 2 万轮验证封顶逻辑 | ✅ 修复（起点 0 / 封顶 1ms） |
| 旧项 | `WorkerQueue` 空弹出虚增 `queue_notfull` 配额 | `sem_post` 曾在 `if` 之外无条件执行 | 现为 `Queue/WorkerQueue/WorkerQueue.c:76` `if(getted == true) sem_post(...)` | ✅ 已修（你此前已改） |
| 备选 | 空闲 CPU 无法到 0% | 无事件通知通道 | sem 配对方案实测 **空闲 0% / c50 51,812**（`epoll=1`） | ⏸ 未采纳（方案存档 §8.2） |

---

## 3. 逐项详解

### 3.1 D-33~36：4 处未校验的 `FdTableFind`（崩溃根因）

**根因链**（`FdConnectManage/FdConnectTable/FdConnectTable.c:41`）

```c
int FdTableFind(Fd_Table* h,int fd,Fd_Entry** ptr)
{
    *ptr=NULL;                                  /* 一进来就清空出参 */
    if(h==NULL||h->Elem==NULL) return 0;
    ...
    if(e==1&&p->next!=NULL&&p->next->fd==fd) {*ptr=p->next; return 1;}   /* 命中 */
    else { return -1; }                         /* 未命中：ptr 仍是 NULL */
}
```

全仓库仅 **4 处**调用点，其中 3 处未校验（第 4 处 `RequestComeMain.c:8` 你此前已加）：

| # | 位置 | 崩在哪 | 修法 |
|---|---|---|---|
| 1 | `FdConnectOrClose/FdConnectOrClose.c:52`（`FdClose()`） | `fd_ob->send_pointer_pool` / `->request_temp_pool` | 第 51 行后加 `if(fd_ob==NULL) return 1;` |
| 2 | `ResponseSend/ResponseSendMain.c:36`（`GetAndJudge()`） | 落到 `:44` `fd_ob->send_pointer_pool->store` | `if(e1!=1\|\|fd_ob==NULL){ if(s.m_queue!=NULL) WorkerQueuePush(...); continue; }` |
| 3 | `ResponseSend/ResponseSendMain.c:92`（`ResponseSendMain()`） | `:93` `fd_ob->ser_nex_send%...` | `if(fd_ob==NULL) return 1;` |

**为什么风暴必然踩到**：`FdClose()` 的所有调用点（`ResponseSendMain.c:44/99/105/138/139`、`Timer/Timer.c:91`、`RequestComeMain.c:17/22/50/64/89`）都在接收线程一条链上；同一批 `events[]` 里同一个 fd 可能被处理两次（EPIPE 先关一次，本批后续迭代或 EPOLLOUT 续写路径再进来一次），第二次 `FdTableFind` 必然 miss。

**踩坑记录（很有价值）**：第一版把第 1 处写成了 `if(fd_ob = NULL) return 1;`（**赋值**），后果比原 bug 更严重——① 顺手把 `fd_ob` 赋成 NULL；② 表达式为假 ⇒ `return` 永不执行；③ 必然落到 `SendPointerPoolDestory(fd_ob->...)` ⇒ **每次 `FdClose` 100% 崩**（实测：curl 两次就死，连风暴都不用，`c50/c200` 直接测不出）。`gcc -Wall` 早已点名：`FdConnectOrClose.c:52:8: warning: suggest parentheses around assignment used as truth value`。改成 `==` 后全部恢复正常。

**返回值语义（易错点）**：`FdConnectManage/Timer/Timer.c:91` 会检查返回值——

```c
int e1=FdClose(w,t->q->queue[1].fd,408);
if(e1!=1) return -1;        /* 返回 0 会让这一轮超时回收提前退出（不崩，只是剩下的超时连接延到下一轮） */
```

故建议第 1 处返回 `1`（语义"已处理完毕"）；返回 `0` 也能跑，两者都不崩，**关键是 `==`**。

**验证**：`/tmp/base_now`（未修）风暴 round 1 后段错误；`/tmp/verify_v5`（你改后）风暴 6×80 全过、存活、`run_tests exit=0`。

### 3.2 D-38：`epoll_wait(...,50)` —— 50ms 长尾与吞吐被压的真凶

**三条代码事实（缺一不可）**

1. **唯一投递口**：能把 Worker 产出写回客户端的 `ResponseSendMain(w,-2,...)`（弹 `Thing_queue` → 存入该 fd 的 send 指针池 → `write()`）**只在 `Threads/ReceiverThread.c:70` 被调用**，即只发生在 `epoll_wait` 返回之后（另一处 `:100` 是 EPOLLOUT 续写路径）。
2. **没有唤醒通道**：Worker 完成走 `ReceiverQueuePush(W->Thing_queue,...)`（`Threads/WorkerThread.c:178/180`），只有互斥 + 背压，**没有 notempty 唤醒**（`Include/Variate.h:50` 是注释）。
3. **连接也不会有事件**：`EpollReadMount` = **EPOLLIN only**（`FdConnectManage/EpollOperate/EpollOperate.c:8`）；只有写不完/`EAGAIN` 才补 `EPOLLOUT`（`:30`）。请求已读完、客户端在等回包 ⇒ 该 fd 既不可读也不可写。

```
请求到 → EPOLLIN 立刻返回 → 排空(无货) → 收包入队 → Worker 微秒级产出 → 推进 Thing_queue
                                                              ↑ 无法唤醒 epoll ★
        ─────────── 白等满 50ms ───────────► 醒来 → 排空 → write() 才发出响应
```

⇒ 实测是 **50.7ms**（不是"平均 25ms"）：**每个响应都至少赔一个周期**，只有高并发时被别的连接事件"顺带"唤醒才提前。

**隔离实验（唯一变量 = 超时值；两版都是不休眠的忙等）**

| 构建 | 空闲 30s 后 3 请求 | 每 1s 一请求 ×5 | c50 |
|---|---|---|---|
| `epoll_wait(...,50)` | 52.6 / 54.3 / 53.7 ms | 51.9~57.0 ms | 9,048 |
| `epoll_wait(...,1)` | 2.9 / 1.4 / 1.4 ms | 1.5~1.8 ms | **31,051（+243%）** |

**连带影响**：① 单请求延迟；② c50 被压在千级；③ `TimerOvertime` 也只在每轮 `epoll_wait` 后调用 ⇒ 超时回收节拍 = 50ms。

**残留风险（可选升级）**：1ms 超时本质仍是轮询（空闲时约 1000 次唤醒/秒，占 CPU 1~5%）。彻底解是 Worker 产出经 `eventfd`/`pipe` 唤醒接收线程的 epoll，此时超时可放回大值。

### 3.3 D-39：静态文件在 keep-alive 上每个 +42 ms（Nagle × 延迟 ACK）

**现象**：同一连接上第 1 个静态请求快，之后每个都 ~43ms；文本请求始终 ~2ms。

```
=== 只剩静态文件 ===          === /hello 与 / 交替 ===
 1 /  3136B    2.79 ms        1 /hello   4.83 ms
 2 /  3136B   43.31 ms        2 /hello   2.53 ms
 3 /  3136B   43.98 ms        3 /        42.33 ms   ← 静态
 4 /  3136B   44.88 ms        4 /hello   2.29 ms
 5 /  3136B   44.23 ms        5 /        43.46 ms
```

**四步定位**

1. **顺序 curl（每次新连接）**：`/` 1.3~1.8ms、`/hello` 1.3~2.0ms ⇒ 与路由/文件大小无关。
2. **strace**（26.17s 日志）：只有 **2 次 write + 2 次 sendfile**，两次响应相隔 **1.7ms**；却有 **15,635 次 `epoll_wait(4,[],1024,1)` 空转**（最大空档 9ms，是 strace 自身开销）⇒ **42ms 不在服务器代码里**。
3. **客户端 `TCP_QUICKACK`**（禁用延迟 ACK）：同一二进制、同一请求 ⇒ **1.69 / 2.07 / 1.44 / 1.99 ms**，42ms 消失。
4. **syscall 形态**：`6 × write(fd=5,len=81)` + `6 × sendfile(fd=5,filefd=6)`（静态=两段） vs `2 × write(fd=5,len=93)`（文本=一段）。

**机理**（经典 Nagle × 延迟 ACK）

- **Nagle（发送方）**：连接上还有未确认数据时，不许再发**小于 MSS** 的段，先在发送缓冲里攒着。
- **延迟 ACK（接收方）**：收到包不立刻 ACK，等能捎带/合并确认；上限 `TCP_DELACK_MAX = HZ/25 = 40ms`（本机 sysctl 未导出该参数）。
- 静态文件 = `write(81B 头)` + `sendfile(3136B 体)` = 两个小段 ⇒ 第 2 段被扣住等 ACK；客户端又必须凑齐 3136B 才能读完 ⇒ 互等 40ms。
- **对照**：`/hello` 单次 `write(93B)` 一个段（不涉及"等第二段"）；**10MB 下载是整 MSS 段，Nagle 不拦**（所以文件越大越快）；连接上第 1 个静态请求因客户端刚发过数据处于 quickack，ACK 立即返回。
- **注意**：`write()/sendfile()` 返回 ≠ 数据上网线——只是交给内核发送缓冲，所以 strace 看到 1.7ms，而线上段 2 在 40ms 后才发出。

**修法对比**

| 方案 | 断在哪一环 | 备注 |
|---|---|---|
| **`TCP_NODELAY`（已采用）** | 发送方 Nagle | accept 后 2 行；nginx/lighttpd 默认 |
| `TCP_CORK`（头 cork→sendfile→uncork） | 反向：确定性合并成一个整段 | 可少一个包，但要处理异常路径 uncork |
| 应用层头+体一次 `write` | 源头：压根没有第二段 | 仅小文件可行 |

**验证（你改完后实测）**：同一连接 4 × `/` 由 `43.3/44.0/44.9/44.2ms` → **`4.30/1.53/1.71/1.61ms`**；`wrk -c10 /` 由 234 req/s → **4,798 req/s（×20.5）**；`website_test` 仍 27 PASS；`/hello` 与 10MB 不受影响。

### 3.4 D-40：空闲 CPU 190~195% 与 Worker 睡眠策略

**三轮迭代**

| 版本 | 代码要点 | 问题 |
|---|---|---|
| v1 无封顶 | `sleepAdd` 每次 `tv_nsec += 100000`，永不归一化 | 第 10,000 次空轮询时 `tv_nsec == 1e9`（非法）⇒ `nanosleep` 返回 `EINVAL` **且不睡眠**，此后每轮都非法 ⇒ **反向退化成永久忙等**（`nano_sim` 状态机 5 万轮验证：从第 10000 轮起全部非法；约连续空闲 **85 分钟**后触发，属"延期爆炸"）。另有 `nanosleep(100µs)`（`µ` 非 ASCII）与 `tv_nesc` 拼写错误 ⇒ 编译不过 |
| v2 加封顶 | `if(tv_sec==1) return 1;` + 命中 1e9 时归零进位 | 合法（非法状态数 0），累计空闲约 83 分钟到 1s 封顶；吞吐代价当时判为 -85%（**后被证伪**，见 §6） |
| **v3 当前** | `sleepInit={0,0}`；`sleepAdd` 每轮 +100µs，`==1000000` 封顶 **1ms** | ✅ 起点 0（不在关键路径给每个请求叠加固定延迟）；`tv_nsec` 全程 ≤1e6 < 1e9 ⇒ **永不越界、无需归一化**；空闲 CPU 1~5% |

**封顶判据的唯一健壮性建议**：`if(ts->tv_nsec == 1000000)` 依赖"步长整除封顶值"（100000｜1000000 ✓）。若日后改步长（如 150000）会漏判，`tv_nsec` 一路涨过 1e9 ⇒ 又变忙等。建议改为 `>= 1000000`（或同时判 `tv_sec > 0`）。

**吞吐代价的正确结论**：同一套 v3 参数，`epoll=50` 时 c50 只有 1,657；改成 `epoll=1` 后 **32,680**。⇒ 之前的"-85% 吞吐"主要来自 D-38，不是睡眠本身（隔离实验证伪，见 §6）。

---

## 4. 验证矩阵（改前 → 改后）

| 指标 | 仓库原始 | 仅修 NULL + `epoll=1` | 再加 `TCP_NODELAY`（当前） |
|---|---|---|---|
| RST 风暴 6 × 80 | **段错误（round 1 后）** | 存活、每轮 0.02~0.03s | 存活（3 × 40 / 6 × 80 均通过） |
| 空闲 CPU | 190~195% | 3~5% | **1%** |
| 空闲后单请求 | 50.7 ms | 1.6 ms | **1.5~1.9 ms** |
| keep-alive 静态首页 `/` | 每个 42 ms | 每个 42 ms | **1.5~1.9 ms** |
| `wrk -c50 /hello` | 1,657 ~ 9,048 | 31,801 ~ 34,964 | **34,413** |
| `wrk -c200 /hello` | 21.6k ~ 24k | 56,261 ~ 59,029 | **56,682** |
| `wrk -c10 /`（静态） | 234 | 234 | **4,798** |
| 10MB 下载（单连接） | 1.1~2.3 GB/s | 1.1~2.3 GB/s | **不变**（见 §11：与本轮改动无关，波动 ±2×） |
| RSS（风暴前后） | — | 3300 → 3788 KB | **3300 → 3300 KB** |
| `test/run_tests.sh` | — | exit=0（23 PASS） | **exit=0** |
| `test/website_test.sh` | — | 27 PASS | **27 PASS / 0 FAIL** |

## 5. 当前性能快照（当前源码、全量重编副本）

| 指标 | 数值 |
|---|---|
| 空闲 CPU / RSS | **1%** / 3300 KB |
| 空闲 12s 后 3 个 `/hello` | 1.49 / 1.61 / 1.84 ms |
| keep-alive 4 × 静态首页 | 3.71 / 1.83 / 1.75 / 1.46 ms |
| `-c50 /hello` | **34,413 req/s**，avg 1.41ms，max 4.35ms |
| `-c200 /hello` | **56,682 req/s**，avg 1.70ms，max 8.23ms |
| `-c50 /`（3136B 静态） | **26,095 req/s**，avg 1.89ms，80 MB/s |
| `-c50 /download/10mb` | **211~449 req/s、2.08~4.41 GB/s**（波动大，机理见 §11：与本轮改动无关） |
| RST 风暴 3 × 40 | 全过、存活；风暴后 `-c50` 35,470 |
| 风暴前后 RSS | 3300 → **3300 KB（零增长）** |

---

## 6. 误判与更正记录（保持可追溯）

| 项 | 当时的判断 | 更正后的事实 | 更正依据 |
|---|---|---|---|
| 吞吐塌陷归因 | "Worker 睡眠落在关键路径，c50 塌陷 -85%" | **主因是 D-38 的 50ms `epoll_wait`**；同一套睡眠参数配上 `epoll=1` 后 c50 由 1,657 → 32,680 | 隔离实验：只改超时值（两版都是忙等）延迟 51ms→1.4ms、c50 9,048→31,051 |
| `FdClose` 返回值 | 早期建议 `return 0` | `Timer.c:91` 检查 `if(e1!=1) return -1;` ⇒ 建议 `return 1`（0 也不崩，只是那轮超时回收提前退出） | 代码审查 |
| guard 命中计数 | grep 报"只改了 1 处" | 是你写了 `fd_ob == NULL`（带空格）而我用 `fd_ob==NULL` 精确匹配 ⇒ 改用 `grep -nE 'fd_ob *== *NULL'` | 逐文件复核 |
| 长尾时长 | 一度以为"最长等 50ms" | 是"**每个响应至少赔一个整周期**"（50.7ms 而非平均 25ms） | 逐请求时序 + 代码路径唯一性 |
| `/tmp` 可用性 | 会话中途 `/tmp` 被系统清理两次（脚本/报告/工作副本一度丢失） | 已全部重建，并冗余备份到 `~/cweb_audit_backup/` | — |

## 7. 测试与复现资产

**仓库根新增（均非源码，可随时删）**

| 文件 | 内容 |
|---|---|
| `CWEB_SUMMARY_REPORT.md` | ← 本报告 |
| `CURRENT_PERF_SNAPSHOT.md` | 一页纸性能快照 |
| `NULL_FIX_HANDOFF.md` | NULL 修复全过程（含逐处改法、踩坑、复核清单） |
| `STATIC_FILE_40MS_FINDING.md` | 42ms 定位四步法 + 机理 + 两种修法 |

**备份目录 `~/cweb_audit_backup/`**

- 脚本：`storm2.sh`（安全版压测）、`keepalive_latency_test.py`、`keepalive_latency_test_quickack.py`、`tcp_nodelay.patch`、`apply_and_verify.sh`
- 日志：`bench_当前源码_综合.txt`、`website_test_通过27.txt`、`website_test_改后27通过.txt`、`keepalive_改后实测*.txt`、`r_笔误版实测.txt`、`r_最终版实测.txt`、`wrk_c10_首页_改后.txt` 等

**一键复现**

```bash
cd /home/yaoxianglongde/c_code/c_web && make -j4          # 必须重编（源码已改）
bash test/run_tests.sh                                     # 单元：exit=0
bash test/website_test.sh                                  # 网站：27 PASS
bash ~/cweb_audit_backup/storm2.sh /home/yaoxianglongde/c_code/c_web 复验 6 40   # 只 kill 自己的 PID
python3 ~/cweb_audit_backup/keepalive_latency_test.py 5343 / / / /              # 期望 ≈1.5~3ms
```

## 8. 遗留项与建议（按优先级）

**8.1 内存池重构（你已构思，我未参与）** — 关注点提示：
- 跨线程归还语义（"绝不跨线程 free"）与 `WorkerQueue` / `Thing_queue` 的引用计数；
- 池满降级路径（响应头/体不足时降级 503）与压力下的行为；
- 与 `RequestTempPool`（请求暂存 + 状态机扩容）、`SendPointerPool`（保序发送暂存）的边界；
- 重构后可复用本报告的验证矩阵（风暴 + c50/c200 + 10MB + RSS 零增长）做回归。

**8.2 sem 唤醒（备选，未采纳）** — 只在需要把空闲 CPU 打到 0% 时再考虑：
- 必须**成对**改：`Queue/TaskQueue/TaskQueue.c/.h` 打开 `sem_task_queue_notempty` 并 `sem_init`；`Threads/WorkerThread.c` 用 `sem_wait` 取代轮询；`WorkerQueuePush`/`TasKQueuePush` 改为**非阻塞**入队；
- 注意 `Include/Variate.h:48/50` 的背压 sem 是按 **D-31** 主动注释掉的（"阻塞式背压已取消"），重新引入要一并梳理 D-31 的语义；
- 历史实测（含 `epoll=1`）：空闲 CPU **0%**、`c50 51,812`。

**8.3 编译告警（当前 15 条，均非本次引入）**
- 值得清：`FdConnectOrClose/FdConnectOrClose.c:45 unused variable 'n'`（`write()` 返回值未处理）、`Threads/ReceiverThread.c:70 'e2'` / `:91 'e0'` / `:96 'e1'`、`FdConnectManage/Timer/Timer.c:87 'fd' set but not used`、`RequestAndResponse/Response.c:8 'n'`、`RequestCome/RequestAnalysis/RequestAnalysiser.c:19 'e0'`；
- 机制类：6 处 `control reaches end of non-void function`（`GlobalResource.c:15`、`MemoryPool.c:42`、`my_rwlock_t.c:23/36/51`、`StaterMain.c:19`、`WorkerThread.c:186`、`SendPointerPool.c:63`）——函数末尾无 `return`，属未定义行为隐患，建议补 `return`。

**8.4 建议记入 backlog 的条目**
- D-38 / D-39 / D-33~36：**已完成**（可作为已解决问题记录）；
- D-42（可选）：静态文件响应头与体合并（`TCP_CORK` 或小文件走单次 `write`）——目标是把"2 个包"变成"1 个包"，中高延迟网络下收益更明显；
- D-43（可选）：`TimerOvertime` 由"每轮 `epoll_wait` 调用一次"改为独立时间片/定时器触发（当前 1ms 超时 ⇒ 空闲时 1000 次/秒的额外唤醒）。

**8.5 本报告未覆盖的边界（未测）**
- 真实网络（非 loopback）下的 Nagle/拥塞控制/高 RTT 行为；
- 慢客户端 / 只写不读（写缓冲填满后 EPOLLOUT 续写路径的长时间压测）；
- 半关闭（`shutdown(SHUT_WR)`）、HTTP pipelining、keep-alive 超时边界（408 路径）；
- 内存池耗尽/降级的压力路径、IPv6、超长 URI/超大头部（413/414/431 的极端值）。

## 9. 时间线

| 阶段 | 内容 |
|---|---|
| 1. 崩溃定位 | 复现 RST 风暴段错误 → 定位 4 处 `FdTableFind` 未校验 → 生成补丁并 A/B 验证 |
| 2. 长尾定位 | 空闲后 50.7ms → 排除睡眠 → 隔离实验锁定 `epoll_wait(...,50)`（D-38） |
| 3. 睡眠复核 | 三轮睡眠实现审查（EINVAL 陷阱、封顶逻辑、起点归零）→ 参数定稿 |
| 4. 崩溃修复落地 | 你手工改 3 处 → 发现 `=` 笔误 → 修正后风暴 6×80 全过 |
| 5. 网站常规测试 | `website_test` 27 PASS；发现 keep-alive 静态文件 +42ms → 四步定位到 Nagle（D-39） |
| 6. Nagle 修复 | 你加 `TCP_NODELAY` → 42ms → 1.6ms、c10 首页 ×20、回归全过 |
| 7. 收尾 | 综合基准 + 本报告 + 文档/日志备份 |

## 10. 源码改动清单（全部由你完成；本报告未改任何源文件）

| 文件 | 改动 |
|---|---|
| `Threads/ReceiverThread.c` | `epoll_wait(w->epfd,events,1024,50)` → `1` |
| `Threads/WorkerThread.c` | `sleepInit` 起点 `100µs` → `{0,0}`；`sleepAdd` 封顶 1s → **1ms**（并修好拼写/非法字符） |
| `FdConnectOrClose/FdConnectOrClose.c` | ① `fd_ob == NULL` 提前返回（`FdClose`）；② `accept` 后 `setsockopt(TCP_NODELAY)` |
| `ResponseSend/ResponseSendMain.c` | ① `if(e1!=1\|\|fd_ob==NULL){ … continue; }`；② `ResponseSendMain` 里 `if(fd_ob == NULL) return 1;` |
| `Include/IncludeStandard.h` | `#include <netinet/tcp.h>` |
| `RequestCome/RequestComeMain/RequestComeMain.c` | （更早）`if(fd_ob==NULL) return 0;` |

> 结论：**功能正确性 ✅　稳定性（风暴/长跑）✅　性能（延迟/吞吐/CPU/内存）✅**；剩余工作（内存池、可选 sem/`TCP_CORK`、告警清理）已在 §8 列出。

---

## 11. 附：10MB 大文件下载速度的机理与 A/B 结论

**先说结论**：大文件吞吐（1~4 GB/s 级）**不是本轮四项优化带来的**，而是这套代码**原本的设计**（`sendfile` 零拷贝 + 非阻塞 + `EPOLLOUT` 续写），叠加"同机 loopback + 页缓存"的测量条件。我们改的三处都与它无关：NULL 校验是正确性；`epoll=1` 只影响**首字节**延迟；`TCP_NODELAY` 对**整 MSS 段无效**。

### 11.1 发送路径（代码级）

1. **路由**：`WebApp/WebApp.c:112 SendFile()` → `open(path,O_RDONLY)` → `ResponseFileSet(w, fd, 0)`；`size_file` 由框架 `fstat` 得到并自动填 `Content-Length`（文件用完由框架 `close(fd)`）。
2. **零拷贝发送**（`ResponseSend/ResponseSendMain.c:137`）：
   ```c
   ssize_t sent = sendfile(send_fd, send_ed_fd,
                           &fd_ob->send_pointer_pool->store[next].offset,
                           size_file - fd_ob->send_pointer_pool->store[next].offset);
   ```
   数据只在内核内流转（页缓存 → socket 缓冲），**不经过用户态缓冲**；发送进度 `offset` 存在**发送暂存池条目**里，可精确续发。
3. **不阻塞收发线程**：连接 socket 在 accept 时就设成 `O_NONBLOCK`（`FdConnectManage/EpollOperate/EpollOperate.c:6-7`）⇒ `sendfile` 发不完立即返回。
4. **`EPOLLOUT` 续写**：发不完（`EAGAIN` 或 `offset` 未到 `size_file`）→ `EpollChangeReAndWr()`（`EPOLLIN|EPOLLOUT`）+ `TimerReset` + return（`:148/153`）；下一个可写事件由 `Threads/ReceiverThread.c:100` 再次进入 `ResponseSendMain(w,fd,…)`，从 `offset` 继续；发完 → `EpollChangeRead()` 摘掉 `EPOLLOUT` 并 `close(文件fd)`（`:158-159`）。
5. **保序**：同一连接按 `serial`/`ser_nex_send` 顺序投递（`FdNextSendPut` + `-2` 排空循环），大文件在途时不会插队。
6. **整 MSS 段**：`sendfile` 填满发送缓冲，内核发出的是整 MSS 段 ⇒ **Nagle 不拦**（这正是大文件不受 42ms 问题影响、`TCP_NODELAY` 对它也无感的原因）。
7. **页缓存**：文件读过一次后常驻 page cache，重复下载测的是内存带宽 ⇒ 2~4 GB/s 属于合理量级。

### 11.2 同口径 A/B（唯一变量 `TCP_NODELAY`；端口改 5443，避免打扰你在跑的 5343 服务）

| 场景 | A：无 `TCP_NODELAY` | B：有 `TCP_NODELAY`（当前） |
|---|---|---|
| 单连接 `curl` ×3（10MiB/次） | 2020 / 2235 / 2328 MB/s | 901 / 1047 / 1980 MB/s |
| `wrk -t2 -c10` | 256 req/s · **2.51 GB/s** | 297 req/s · **2.90 GB/s** |
| `wrk -t2 -c50` | 211 req/s · **2.08 GB/s** | 421 req/s · **4.13 GB/s** |

⇒ 差异**方向不一致、幅度落在同构建重复测的波动区间内**（2.08 ~ 4.41 GB/s）⇒ **无一致性差异**。早前表格中"1122 MB/s → 4.41 GB/s"的并列写法**口径不一致（单连接 vs c50）**，已在本报告 §4/§5 与 `CURRENT_PERF_SNAPSHOT.md` 中更正。

### 11.3 若还想更快（可选，非必需）

- **循环发送到 `EAGAIN`**：当前每次 `EPOLLOUT` 只调用一次 `sendfile` 就返回（`:146-151`），可改为 `while` 循环直到 `EAGAIN`/发完，减少 epoll 往返轮次（大文件 + 高延迟网络收益更明显）。
- **`TCP_CORK` / 合并响应头**：对 10MB 无意义（整段已满），主要是给小文件省一个包。
- **公平性**：`c50` 并发下载时平均延迟 78~180ms 属排队现象；若在意可限制单轮发送字节数，避免大文件独占接收线程的一轮。





