# c_web 缺陷总清单（自并发压测起的全部问题，按紧要程度排序）

## 0. 怎么用这份清单

### 0.1 这份清单的定位与来源

**这是唯一的待办总表**，把压测以来所有轮次的发现合并、去重、重排优先级：

| 来源 | 内容 | 现存位置 |
|---|---|---|
| `test/STRESS_TEST_REPORT.md` | BUG-11 ~ BUG-17、OPT-01 / OPT-02 | ✅ 仓库内 |
| `test/TEST_REPORT.md` | BUG-01 ~ BUG-10 的处置现状、残余告警面 | ✅ 仓库内 |
| 内存泄漏专项（进程级 valgrind + 单测 valgrind） | L-1 ~ L-6 | ⚠️ 原稿在 `/tmp/boundary/MEMORY_LEAK_REPORT.md`，**`/tmp` 已被清空**；结论已逐条并入本清单 |
| 边界与容量专项 | 内存墙 / 并列上限 / 限流设计 | ⚠️ 原稿在 `/tmp/boundary/BOUNDARY_REPORT.md`，**已被清空**；限流设计已重新落到 `test/RATELIMIT_DESIGN.md` |
| **本轮重新逐条核实（2026-10-14）** | **新增 4 条必崩路径（D-04 / D-08a-c / D-17c）＋ 1 条堆越界写（D-16 T3）＋ 1 条关闭地雷（D-30）**，并确认 S0/S1 各项**在当前工作区仍全部成立** | ✅ 就是本文件 |

> ⚠️ **关于 `/tmp`（本轮的重要发现）**：`/tmp/boundary/` 已被系统清空，早前放在那里的中间报告、
> valgrind 日志与探针源码（`probe_wq.c` 等）**全部丢失**。
> **因此从本文件起，所有结论与设计只落在仓库内（`test/`），不再依赖 `/tmp`。**

> ✅ **本轮复测基线（clean build）**：`make clean && make -j4` → **rc=0**；
> `make -C test run` → **`==== 全部单元测试通过 ====`，rc=0（286 PASS / 0 FAIL）**；（本轮由 256 增至 286，见变更日志）
> `-Wall` 下残余告警 **17 条**（清单见 §6.4）——**单测全绿并不代表 S0 不存在**，
> 因为这些缺陷是时序/容量类，单测结构上覆盖不到（见 `STRESS_TEST_REPORT.md` 2.1）。

### 0.2 核实基准（重要，避免拿旧结论办事）

- 基线提交：`314174f`（`main`，「编译通过」）+ 已跟踪文件的未提交改动
  （`EpollOperate.c`、`RequestAnalysiser.c`、`RequestIntergrity.c`、`StarterMain.c`、`main.c`、`Makefile`）。
- **本清单 S0/S1 的全部缺陷文件均处于原始提交状态**（`WorkerQueue.c` / `MemoryPool.c` / `WorkerThread.c` /
  `FdConnectOrClose.c` / `Timer.c` / `RouterHash.c` / `ReceiverThread.c` / `RequestComeMain.c` /
  `RequestTempPool.c` / `StateMachine.c`），即**框架既有缺陷，非本轮开发引入**。
- 核实日期：2026-10-14。核实方式：逐行读源码 + 进程级 valgrind + 自研探针（不是复述旧报告）。
- 每一节都标注 **状态**：`仍存在（已核实）` / `已修复` / `死代码` / `需复现确认`。

### 0.3 分级定义

| 级别 | 含义 | 处理时机 |
|---|---|---|
| **S0** | 会让进程崩溃、或静默毁坏内存池数据 | 随时可修，**优先** |
| **S1** | 不会立刻崩，但会静默丢数据 / 持续泄漏 / 归还错误 | S0 之后立刻 |
| **S2** | 结构性定时炸弹（容量策略、生命周期、限流），改起来要动设计 | 需要专门一轮 |
| **S3** | 性能（有实测倍数，改一行或改设计） | 与 S0/S1 并行 |
| **S4** | 工程与清理（告警、测试自身问题、CI 护栏） | 碎片时间 |

### 0.4 一页速览（S0）

| 编号 | 级别 | 一句话 | 位置 | 状态 |
|---|---|---|---|---|
| **D-01** | S0 | 队列**空弹出仍 `sem_post`** → 容量闸门失效 → 越容量写入 | `WorkerQueue.c:72` | 仍存在（已核实） |
| **D-02** | S0 | 池**无归属校验**：同一块可被归还两次、发放两次 | `MemoryPool.c:136-146`、`:211` | 仍存在（已核实） |
| **D-03** | S0 | `FdClose` **不检查 `FdTableFind` 返回值** → `NULL` 解引用 | `FdConnectOrClose.c:50-56` | 仍存在（已核实） |
| **D-04** | S0 | **新**：`RspHeaderAlloc` 池满时 `Rsp->headers==NULL` 却继续解引用 → **必崩** | `WorkerThread.c:8-23` | 仍存在（已核实） |
| **D-05** | S0 | **每连接泄漏 12 B**（`State_Machine` 无回收） | `StateMachine.c:5` / `RequestTempPool.c:26-35` | 仍存在（实测） |
| **D-06** | S0 | `char* C;` **未初始化** + 503 分支不赋值（UB） | `WorkerThread.c:135`、`:49-50` | 仍存在（已核实） |
| **D-07** | S0 | 4 处关键返回值被丢弃（含"请求静默丢失"） | `ReceiverThread.c:25` 等 | 仍存在（已核实） |
| **D-08** | S0 | **新**：另 3 处 `FdTableFind` 返回值被丢弃 → 同族 `NULL` 解引用 | `RequestComeMain.c:8-11`、`ResponseSendMain.c:89-90`、`:36-42` | 仍存在（已核实） |
| **D-31** | S0 | **新（本轮，已修）**：任务队列 CAS 改造把**计数**与**环形指针**放进两个同步域 ⇒ 虚假弹出 → `W==NULL` ⇒ **段错误** | `Queue/TaskQueue/TaskQueue.c` | ✅ **已修复（本轮）**，附前后实测 |

### 0.5 一页速览（S1 ~ S4）

| 编号 | 级别 | 一句话 | 位置 | 状态 |
|---|---|---|---|---|
| **D-09** | S1 | `Alloc` 单位 KB、`Free` 单位字节（迟早归还错尺寸类） | `MemoryPool.c:173` / `:197` | 仍存在（已核实） |
| **D-10** | S1 | `TimerExpend` 漏 `free` 旧数组 + 新容量按 **fd 数值**算 | `Timer.c:16-31` | 仍存在（实测 48 B） |
| **D-11** | S1 | 路由表扩容丢整张表（3,880 B/次，>34 条路由触发） | `RouterHash.c:136-182` | 仍存在（实测） |
| **D-12** | S1 | `MemoryStackExpend` 漏 `free(leisure)` + 锁错对象 + 无 `return` | `MemoryPool.c:25-42` | 仍存在（死代码） |
| **D-13** | S1 | `MMemoryStackPush` **无上界检查**（重复 serial 的放大器） | `MemoryPool.c:44-52` | 仍存在（已核实） |
| **D-14** | S1 | `MemoryPoolFree` 校验失败直接 `return 0` **放弃归还** | `MemoryPool.c:211` | 仍存在（已核实） |
| **D-15** | S1 | 暂存区扩容：拷贝长度用整块大小、少 1 字节、含未初始化尾部 | `RequestTempPool.c:71-86` | 仍存在（已核实） |
| **D-16** | S1 | **新**：定时器把 `queue[0].fd` 当元素计数用；`TimerReset` 未命中时越界写 | `PriorQueueTimer.c:23/117`、`Timer.c:65-80`、`:91` | 仍存在（已核实） |
| **D-17** | S1 | 两个"并列上限"：每 Worker `http_pool` **只有 10 块**；`ed_store_blocknum=100` vs send 池 400 | `WorkerThread.c:78`、`RequestComeMain.c:10-11` | 仍存在（已核实） |
| **D-18** | S2 | **扩容机制整体废弃** → 5 个扩容点的逐点处置方案（本轮决定） | 见 §3 | 待决策落地 |
| **D-19** | S2 | `MemoryEntryExpend` 搬 strip 会让**已发出的块指针全部悬垂** | `MemoryPool.c:85-106` | 死代码，但必须删 |
| **D-20** | S2 | 全库**只有 2 个 `Destory`** → 无销毁接口、无优雅关闭（84 MB 常驻） | 全局 | 仍存在 |
| **D-21** | S2 | 无统一限流（`test/RATELIMIT_DESIGN.md` 方案未落地）→ 并发上限仍是硬需求 | 全局 | 未实施 |
| **D-30** | S2 | **新**：线程入口参数指向 `StartThread` 栈帧（靠"永不返回"侥幸成立）⇒ 做优雅关闭必崩 | `StaterMain/StarterMain.c:60-84` | 仍存在（已核实） |
| **D-22** | S3 | **缺 `TCP_NODELAY`**：静态页 QPS 1,049 → 11,777（11.2×） | `FdConnectOrClose.c:16` 后 | 仍未加（已核实） |
| **D-23** | S3 | `Connection: close` 硬编码，无 keep-alive | `FdConnectOrClose.c:95` | 仍存在（已核实） |
| **D-24** | S3 | 未归因：P90 残留 40 ms、QPS 随并发升高、accept 路径串行开销 | 全局 | 未归因 |
| **D-25** | S4 | `RequestAnalysiser.c:19` 分配结果未检查（`-Wunused-variable`） | `RequestAnalysiser.c:19` | 仍存在 |
| **D-26** | S4 | 残余编译告警（含 `MemoryPool.c:42` 非 void 函数无 `return`） | 全库 | 仍存在 |
| **D-27** | S4 | CI 缺「ASan + 30 s 并发压测 + 探针断言」护栏 | `test/run_tests.sh` | 未实施 |
| **D-28** | S4 | 测试自身泄漏（`test_response` 263 KB、`test_hash` 8 KB） | `test/src/` | 仍存在 |
| **D-29** | S4 | 单元测试无内存池/队列不变式断言（压测报告 10.2 的 4 条） | `test/src/` | 未实施 |
| **D-32** | S3 | **新（本轮）**：任务队列取消阻塞后业务线程**无等待机制** ⇒ 空闲时 CPU **197~200%**（2 核跑满） | `Threads/WorkerThread.c` + `TaskQueue.c` | 仍存在（已实测） |

### 0.6 修复节奏与依赖顺序（照这个顺序修不会白干）

```
第 1 步：D-01（1 行）＋ D-13（3 行）＋ D-02（加校验）   ← 止住"内存池被毁"这条主链
第 2 步：D-03 / D-04 / D-08 / D-07    ← 四条必崩路径，都是"检查返回值 + 提前 return"
第 3 步：D-05（3 行）＋ D-06（2 行）    ← 止住泄漏与 UB
第 4 步：D-09 / D-14 / D-16 / D-15     ← 单位、归还、越界、拷贝长度
第 5 步：D-18（扩容废弃）+ D-19 / D-10 / D-11 / D-12  ← 专门一轮
第 6 步：D-22 / D-23（性能）           ← 可任意时间做，D-22 独立无风险
末    步：D-17 → D-21（容量自检 + 限流）← 必须等前面稳了再上
关闭轮：D-30（线程入参改堆/静态）→ D-20（Destory + 优雅关闭）← 顺序不能反，见 D-30
```

**两条硬性先后关系：**
1. **`TCP_NODELAY`（D-22）随时可加**，但它会让 QPS 提高约 11 倍 ⇒ 并发压力同步上升 ⇒
   **建议至少在 D-01 / D-02 / D-03 / D-04 / D-08 修完后再做**，否则更容易撞上崩溃。
2. **不要先做 keep-alive（D-23）和限流（D-21）**：两者都会把"每连接一次"的开销/时序缺陷
   变成"每请求一次 / 高频一次"，会把 S0 的偶发崩溃放大成必现。

---

## 1. S0 —— 会崩 / 会毁数据（9 条）

### D-31 · 任务队列 CAS 改造：计数与环形指针分属两个同步域 ⇒ 虚假弹出 ⇒ 段错误

- **状态**：✅ **已修复（本轮）**（`Queue/TaskQueue/TaskQueue.c`），并附修复前后实测对比。
- **先回答"还会死锁吗"**：**不会以"阻塞死锁"的形式发生了**（活性判据全绿：生产者推进完、
  队列能排空、无永久拒绝）。**但换成了更严重的"段错误 + 数据错乱"** —— 见下表。
- **背景**：原版用 `sem_wait(notfull)` / `sem_wait(notempty)` 阻塞式背压。其死锁路径是：
  **收发线程**在 `ReceiverToWorker`（`Threads/ReceiverThread.c:50-56`）里阻塞在满队列上时，
  整个 epoll 循环停摆 —— `TimerOvertime`（超时关连接）与 `ResponseSendMain`（发响应）都不再执行
  ⇒ 业务线程等任务、收发线程等空位，**双方互等 ⇒ 全站卡死**。
  **"改成非阻塞"这个方向是对的**，但落地时踩了 3 个坑。
- **三个结构性缺陷**（`git diff` 可见改动全貌）：
  1. **计数重复**：CAS 已经把 `num` ±1，进互斥区后又做了一次 `sq->num++` / `sq->num--`
     ⇒ `num = 2 × 真实在队数` ⇒ **容量被砍半**（容量 4 只能装下 2 个）。
  2. **非原子与原子混用**：`int before = sq->num;`（普通读，`:41` / `:83`）与
     `atomic_compare_exchange_strong(&sq->num, …)`（`:44` / `:87`）构成数据竞争；锁内的
     `sq->num++/--` 也是非原子 RMW ⇒ **丢失更新**，`num` 随机漂移（实测漂成**负数**）。
  3. **跨同步域**：`num` 走 CAS（无锁）、`ptr_in/ptr_out` 走 mutex ⇒ CAS 放行了 N 次引用，
     环形指针只移动了 N−1 次 ⇒ **pop 返回成功，但槽位里仍是上一轮的残留条目**。
- **实测证据（全部可在仓库内复跑，不依赖 `/tmp`）**：

  | 判据 | 修复前 | 修复后 |
  |---|---|---|
  | 多线程探针 `test_taskqueue_race`（2×20000 投递 / 2 消费者 / 容量 64） | FAIL 8 / PASS 11（3 次运行一致） | **PASS 23 / FAIL 0** |
  | `pop` 成功数（应恰等于 `push` 数 40000） | **70543 / 55206 / 82680** | **40000** |
  | 虚假弹出（队列逻辑为空却 pop 成功） | **30543 / 15206 / 42680** | **0** |
  | 同一条目被重复取出 | **67273 / 49269 / 73473** | **0** |
  | 收尾 `num`（应为 0） | **−60422 / −30242 / −85007** | **0** |
  | TSAN | `data race`：`:44` 原子写 vs `:81` 普通读 | **0 报告** |
  | 真实服务器 `website_test.sh` | **PASS 6 / FAIL 21 ＋ 段错误**（第 2 个请求即崩） | **PASS 27 / FAIL 0** |
  | gdb 崩溃栈 | `WorkerMain` `Threads/WorkerThread.c:141`（两个 Worker 同点崩） | — |

- **崩因链**：`num` 漂移 → 容量闸门失效（`push` 再也不返回 0，实测"拒绝 0"）→ 环形缓冲被
  反复覆盖 ＋ 空队列仍返回"残留条目" → `TasKQueueTopAndPop` 交回 `w == NULL`
  （`TasKQueueInit` 的初值）→ `WorkerMain` 解引用 `W->Thing_queue`
  ⇒ **SIGSEGV**（`WorkerThread.c:141`，`:113` 同族）。
- **修法（已落地）**：保留"非阻塞"意图，把**满/空判定与计数增减全部收进同一把互斥区**：
  · `push`：锁内 `if(num==blocknum){ unlock; return 0; }`（非阻塞背压）；
  · `pop` ：锁内 `if(num==0){ unlock; return s; }`（非阻塞空返回）。
  删掉 4 处 CAS 与重复的 `num++/--`，`num` 恢复为"锁内单点维护"的唯一真相。
- **验收**：`make -C test run` 全绿；`bash test/website_test.sh` **27/27**；
  探针在 `-O0` 与 **TSAN** 下均 **PASS 23 / FAIL 0** 且 TSAN 零报告。
- **测试用例同步**：`test/src/test_taskqueue_race.c`（新增，注册进 `test/Makefile` 与
  `TESTS`）＋ `test/src/test_queues.c` 中 4 条**旧阻塞语义**断言已改写为**非阻塞背压语义**
  （这是"阻塞→非阻塞"设计变更的必然结果，不是放宽标准：新断言额外覆盖了
  "满队列 push 立即返回 0 / 被拒的 push 不改动计数 / 空出槽位被复用"）。
- **关联**：**D-01 是同一族**（"计数与闸门失配"），D-31 是它在 TaskQueue 上的翻版；
  修 D-01 时请对照本条判据思路（守恒式断言＋TSAN）。
- **收尾**：`Include/Variate.h:48/52` 的两条 `extern sem_t sem_task_queue_not*`（无定义、无引用）
  已一并注释；`TaskQueue.h` 内的 `sem_task_queue_*` 字段此前已注释。

### D-01 · BUG-11：队列空弹出仍归还容量 ⇒ 容量闸门失效

- **状态**：仍存在（已核实源码 `WorkerQueue.c:72`；本轮探针复现）
- **位置**：`Queue/WorkerQueue/WorkerQueue.c:72` —— `sem_post(&(sq->queue_notfull));` 在
  `if(sq->num!=0){...}`（`:60-69`）**之外**，也就是"没弹出任何东西"也会归还一个配额。
- **触发**：**任何并发压测**。`CleanMemoryQueue`（`Threads/WorkerThread.c:61-66`）是 `while(e1!=0)` 循环，
  收尾时必然多走一次空 pop ⇒ **每轮至少 +1、永不归还**。
- **实测证据**：压测报告 4.5 记录 35 s 内 **18,748 次**空弹出 post，信号量 `100 → 101 → 102 …`；
  本轮独立探针（容量 3）：5 次空 pop 后 `notfull` **3 → 8**，随后 `push` 不再阻塞，`num=5 > 容量3`。
- **后果链（每一环都有实测）**：
  ```
  空弹出仍 post → notfull 无上限漂移 → WorkerQueuePush 的 sem_wait 不阻塞
    → 100 槽环形队列被越容量写入 → ptr_in 绕圈覆盖尚未取走的条目
    → 同一个 char_ptr 在队列里出现两次 → MemoryEntryFree 对同一块二次归还
    → 空闲栈出现重复 serial → 同一块被分配给两个使用者
    → strcat 源目重叠（ASan 实测 55 次，两个区间起始地址完全相同）→ 偶发 SIGSEGV
  ```
- **修法**：把 `:72` 的 `sem_post` **移进** `if(sq->num!=0)` 块内（放在 `*error=1;` 之后、`return s;` 之前），
  空队列分支只 `pthread_mutex_unlock` 后 `return s;`，不 post、不改任何状态。**共 1 行搬移。**
- **验收**：带探针构建下「空弹出计数 = 0」且「信号量值恒 ≤ 容量」；`wrk -t2 -c200 -d30s /hello` 连续 3 轮无崩溃。
- **关联**：D-02 / D-13 是同一链路的后两道防线，建议同批修。

### D-02 · BUG-12：内存池没有"归属校验"，同一块可归还两次 / 发放两次

- **状态**：仍存在（已核实源码）—— `MemoryEntryFree` 只按地址反算 serial，**不校验该块是否处于"已分配"状态**：
  ```c
  // MemoryPool/MemoryPool.c:136-146
  int serial=total_gap/(size*1024);
  MMemoryStackPush(e->stack,serial);   // ← 无条件压回空闲栈
  e->haded_num--;
  ```
  `MemoryPoolFree`（`:211`）只做**范围**校验（挡"不是这块内存"，**挡不住"同一块还两次"**）。
- **触发**：由 D-01 诱发的重复归还；或 D-09 的单位错配导致块被归还进错误尺寸类。
- **实测证据**：压测报告 4.4 —— 探针直接抓到「释放了一个不在分配状态的块」；ASan `strcat-param-overlap` ×55。
- **修法（二选一，建议都做）**：
  1. **廉价方案**：给 `Memory_Entry` 加一张"分配位图"（或 `int* allocated` 数组），
     `MemoryEntryAllocTwo` 置位、`MemoryEntryFree` 校验并清位，校验失败则 **记日志 + 拒绝归还**（而不是压栈）；
  2. **公式校验**：`haded_num` 与空闲栈 `top` 应满足 `top + haded_num == strip_num`，任一分配/归还操作后断言。
- **验收**：压测 30 s 内不出现"释放了不在分配状态的块"告警；单测新增位图不变式用例。

### D-03 · BUG-13：`FdClose` 丢弃 `FdTableFind` 返回值 ⇒ 空指针解引用

- **状态**：仍存在（已核实 `FdConnectOrClose.c:50-56`，`fd_ob` 初始化为 `NULL`，`:51` 返回值丢弃，`:54/:56` 直接解引用）
- **触发**：**重复关闭**。`FdClose` 会被"正常收尾 / 定时器 408 / 解析出错"多条路径调用，
  只要 timer 已关过连接、随后 epoll 又投递一个事件（或同一 `epoll_wait` 批次里两次命中同一 fd），
  第二次进来 fd 已从表中摘除 ⇒ `fd_ob == NULL` ⇒ 崩。
- **修法**：加一行守卫（`FdTableFind` 本身已经把 `*ptr` 置 NULL，语义干净）：
  ```c
  Fd_Entry* fd_ob=NULL;
  if(FdTableFind(w->fd_table,client_fd,&fd_ob)!=1||fd_ob==NULL) return 0;  // ← 新增
  ```
- **注意**：修完后 `FdClose` 变成"幂等"的（第二次调用直接返回），这正好是后面 D-08 的前置。
- **验收**：构造"超时关闭 + 同时投递可读事件"的探针，进程不崩。

### D-04 · 新发现：`RspHeaderAlloc` 池满后**空指针解引用 → 必崩**

- **状态**：仍存在（已核实 `Threads/WorkerThread.c:8-23`）—— 这是本轮新抓到的**第 4 条崩溃路径**，
  且比 BUG-14 更严重（BUG-14 是"未初始化"，这条是**真解引用**）：
  ```c
  void* ptr=NULL;
  int notfull=0;
  MemoryPoolAlloc(profes->txt_pool,63,&ptr,&notfull);   // 固定申请 63KB ⇒ 落在 64KB 尺寸类
  if(notfull==0)  Rsp->error_reason=503;                // ← 只设 503，没有 return！
  Rsp->headers=ptr;                                     // ptr 仍是 NULL（池满时未被赋值）
  ptr=ptr+sizeof(Response_Header_Entry);                // 对 NULL 做指针运算
  Rsp->headers->begin=ptr;                              // ★ 写地址 ≈0+32 ⇒ SIGSEGV
  ```
- **触发条件**：`txt_pool` 的 **64 KB 尺寸类用尽**（`WorkerThread.c:77` `MemoryPoolInit(&txt_pool,7,7,200,120)`
  ⇒ 前 7 个尺寸类 1/2/4/8/16/32/64 KB 各 **120 块**）。只要同时有 >120 个响应在打包，必然命中。
  与 §1.7 实测的"~400 那道墙"是同一族现象的上游。
- **修法**：`if(notfull==0){ Rsp->error_reason=503; Rsp->headers=NULL; return 0; }`
  并且**调用点 `WorkerMain`（`:107`）要检查返回值**，失败时跳过整个打包阶段直接走 503 回包。
- **验收**：把 `txt_pool` 的 64 KB 类人为调小（临时改成 2 块）跑一遍高并发，进程不崩、全部回 503。
- **关联**：D-06（同函数族的"隐式契约"）、D-07（返回值不检查的普遍习惯）。

### D-05 · 每连接泄漏 12 字节（`State_Machine` 全库无回收口）

- **状态**：仍存在（**进程级 valgrind 实测**，非推断）
- **位置**：分配在 `FdConnectResource/RequestTempPool/StateMachine/StateMachine.c:5`（`malloc(sizeof(State_Machine))`），
  回收侧 `RequestTempPoolDestory`（`RequestTempPool.c:26-35`）**只 `free(h)`**，而 `RequestTempPoolFree`（`:55-67`）
  只处理缓冲区、完全不碰 `h->state_machine`。**全库搜索不存在 `StateMachineDestory`**。
- **实测证据**：真实服务器 + 200 次 `curl` → `definitely lost: 2,400 bytes in 200 blocks` = **12 B / 连接**，
  调用栈完整闭合：`StateMachineInit(StateMachine.c:5) ← RequestTempPoolInit(RequestTempPool.c:14)
  ← FdTableInsert(FdConnectTable.c:77) ← FdConnect(FdConnectOrClose.c:32)`；
  单元测试 `test_temp_pool` 也报同一条 **12 B**（第二个复现点）。
- **★ 关键性质**：它是**"连接总数"的函数，不是"并发数"的函数** ⇒ **任何限流（D-21）都管不到这一维**：
  并发压到 80，跑 1000 万次短连接照样泄漏 120 MB。
- **修法（3 处）**：
  ```c
  // ① StateMachine.h 加声明
  int StateMachineDestory(State_Machine* h);
  // ② StateMachine.c 加实现
  int StateMachineDestory(State_Machine* h){ if(h==NULL) return 0; free(h); return 1; }
  // ③ RequestTempPool.c:26-35 Destory 中，free(h) 之前插入
  if(h->state_machine!=NULL){ StateMachineDestory(h->state_machine); h->state_machine=NULL; }
  ```
- **验收**：重跑「200 连接 valgrind」⇒ `definitely lost` **由 2,400 B 降为 0**。

### D-06 · `char* C;` 未初始化 + 503 分支不赋值（UB 与隐式契约）

- **状态**：仍存在（已核实 `Threads/WorkerThread.c:135`、`:44-52`）
  ```c
  // WorkerThread.c:135（WorkerMain 内）
  char* C;                                   // ← 未初始化
  // WorkerThread.c:44-52（PackTotal 内）
  *allocsize=strlen(Rsp->headers->begin)+Rsp->body_size+17;
  MemoryPoolAlloc(profes->txt_pool,*allocsize/1024+1,&ptr2,&notfull2);
  if(notfull2==0)  Rsp->error_reason=503;     // ← 走这条分支时 *C 完全没被赋值
  else  *C = ptr2;
  PackBackHead(C,Rsp);
  ```
- **诚实评估**：`PackBackHead` 在 `error_reason!=200` 分支（`:167-181`）**并不读 `*c`**，
  所以当前**不会立刻崩**。但：① 未初始化的 `char*` 参与传参是 UB，编译优化/后续重构下随时变真问题；
  ② "池满就不赋值"是**无注释、无断言的隐式契约**，下一个人十有八九会踩。
- **修法（2 行）**：`char* C=NULL;` ＋ `if(notfull2==0){ Rsp->error_reason=503; *C=NULL; }`
- **前置**：`:44` 的 `strlen(Rsp->headers->begin)` 在 D-04 未修时会**先崩在 NULL 解引用上**，
  所以这条要**在 D-04 之后**做（D-04 修好后 `headers` 为 NULL 时会在 `WorkerMain` 提前走 503，不再进入 `PackTotal`）。

### D-07 · BUG-17：4 处关键返回值被丢弃（含"请求静默丢失"）

- **状态**：仍存在（逐行核实）

| 位置 | 被丢弃的返回 | 后果 | 备注 |
|---|---|---|---|
| `MemoryPool.c:128`（`MemoryEntryAllocTwo`） | `MemoryStackTopAndPop` | 弹出失败时 `serial` 仍为 `-1` ⇒ `ptr = memory_strip - size`，**越界指针**进入后续 `memcpy`/`strcat` | **在产路径**（`MemoryPoolAlloc:191` 调用），优先修 |
| `MemoryPool.c:114`（`MemoryEntryAlloc`） | 同上 | 同上 | **死代码**（全库无调用者）→ 建议随 D-19 一起删 |
| `Threads/ReceiverThread.c:25` | `TasKQueuePush` | 任务队列满 ⇒ 请求**静默丢弃**，客户端收不到任何响应、干等到超时 | 且该函数非 `void` 却**没有 `return`**（同 BUG-06 家族） |
| `FdConnectOrClose.c:32` | `FdTableInsert` | 插入失败仍继续挂 timer/epoll ⇒ 连接**不在管理表中**，后续所有 `FdTableFind` 失败 | 见 D-08 的连锁 |

- **同族扩展（本轮新列）**：`FdConnectOrClose.c:26` `EpollReadMount`、`:29` `TimerAlloc` 的返回值同样没检查；
  `FdConnect` 整个函数**没有失败回滚**——若 `FdTableInsert` 失败，已经挂上的 epoll 关注不会撤销，fd 也不会 `close`。
- **修法**：
  ```c
  // ReceiverThread.c:25
  return TasKQueuePush(taskQueue,w,fd,serial,error_reason,h,h_size);
  // 调用方 RequestComeMain.c:71 检查失败 ⇒ FdClose(Receiver_Thread,fd,503);
  // FdConnectOrClose.c:32
  if(FdTableInsert(w->fd_table,client_fd)!=1){ EpollDelete(w->epfd,client_fd); close(client_fd); return -1; }
  ```
- **验收**：把 `taskQueue` 容量临时调小（如 1）跑压测，客户端应收到 503 而不是干等超时。

### D-08 · 新发现：另 3 处 `FdTableFind` 返回值被丢弃 ⇒ 同族空指针解引用

- **状态**：仍存在（本轮逐行核实，是除 D-03 之外**另外三条独立崩溃路径**）
- **位置与触发**：

| # | 位置 | 代码 | 触发场景 |
|---|---|---|---|
| D-08a | `RequestComeMain.c:8-11` | `FdTableFind(...);` → 下一行 `if(fd_ob->pack_in_path>=ed_store_blocknum)` | fd 已不在表中 ⇒ `fd_ob==NULL` ⇒ **解引用 NULL** |
| D-08b | `ResponseSendMain.c:89-90` | `FdTableFind(...);` → `int next=fd_ob->ser_nex_send%...` | 同上 |
| D-08c | `ResponseSendMain.c:36-42` | `if(e1!=1&&s.m_queue!=NULL){ ...; continue; }` → 落到 `fd_ob->send_pointer_pool->store` | **`e1!=1` 且 `m_queue==NULL`（错误响应包）时不会 `continue`** ⇒ `fd_ob==NULL` 解引用 |

- **D-08a 的具体时序**（最值得复现）：同一个 `epoll_wait` 批次里，`ReveiverThreadMain:57` 先
  `TimerReset`、`TimerOvertime`（`:46`，在上一轮内层循环）已经把超时连接 `FdClose` 掉，
  随后 `events[i]` 里仍有该 fd 的 `EPOLLIN` ⇒ `RequestComeMain` 拿着一个已摘除的 fd 进来 ⇒ 崩。
  *（该场景还会同时命中 D-16 的 `TimerReset` 越界写，所以一次能带走两个 bug。）*
- **修法（三处统一守卫）**：
  ```c
  // RequestComeMain.c:8
  if(FdTableFind(Receiver_Thread->fd_table,fd,&fd_ob)!=1||fd_ob==NULL) return 1;
  // ResponseSendMain.c:89
  if(FdTableFind(w->fd_table,send_fd,&fd_ob)!=1||fd_ob==NULL) return 1;
  // ResponseSendMain.c:36  —— 条件从「且 m_queue!=NULL」改成「不管 m_queue 是谁，只要拿不到连接就走同一处理」
  if(e1!=1||fd_ob==NULL){ if(s.m_queue!=NULL) WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr); continue; }
  ```
- **验收**：三处加守卫后，用「低超时(1s) + 高并发」压测 30 s 不崩（现在该组合必崩）。

---

## 2. S1 —— 静默错误 / 泄漏 / 归还错误（9 条）

### D-09 · BUG-15：`Alloc` 参数单位是 **KB**，`Free` 参数单位是**字节**

- **状态**：仍存在（已核实 `MemoryPool.c:173` vs `:197`）
  ```c
  int MemoryPoolAlloc(Memory_Pool* p,int size,void** ptr,int* notfull)   // size 单位 = KB
  {   ...
      if(size>p->pool[p->Entry_num-1].strip_size){ *ptr=malloc(size*1024); return 1; }  // ×1024
  int MemoryPoolFree(Memory_Pool* p,void* ptr,int size)//单位字节        // size 单位 = 字节
  {   if(size>p->pool[p->Entry_num-1].strip_size*1024) { free(ptr); return 1; }        // 直接比
  ```
- **为什么危险**：两个函数只隔 20 多行，单位却不同，全靠每个调用点手写 `/1024`、`*1024` 对齐。
  **任何一处漏写，块就会被归还进错误的尺寸类** ⇒ 直接触发与 D-02 完全一样的池损坏。
  本次核实到的手写换算点（供逐点复查）：
  `WorkerThread.c:12`（`63`）、`:47`（`*allocsize/1024+1`）、`:64`、`:170`、`:174`（`64512`）、`:189`、`:193`、`:209`；
  `RequestComeMain.c:62`（`h_size/1024+1`）；`RequestTempPool.c:41`、`:58`。
  其中 `174` / `189` 用**硬编码 `64512`**（=63 KB）去归还 `headers`，一旦 `RspHeaderAlloc` 的分配尺寸改了就会错配。
- **修法**：统一为**字节**（更符直觉，且与 `Free` 的注释一致），参数改名 `bytes`；`Alloc` 内部
  `int need_kb=(bytes+1023)/1024;`。顺手加断言 `assert(size%1024==0)` 或改用显式 `_kb` 后缀命名。
- **验收**：`test_memory_pool` 增加"分配 N KB → 用字节数归还"的往返用例。

### D-10 · BUG-16/L-2：`TimerExpend` 漏 `free` 旧数组 + 新容量按"fd 数值"算

- **状态**：仍存在（**单测 valgrind 实测 48 B**）
  ```c
  // FdConnectManage/Timer/Timer.c:16-31
  PriorQueueTimerInit(&q2,2*(t->q->queue[0].fd));   // ← 拿"元素计数"当容量（queue[0].fd 是计数器，见 D-16）
  for(int i=0;i<t->q->end;i++){ ... }
  q2->elem_end=t->q->elem_end;
  free(t->q);        // ★ 只 free 了结构体，t->q->queue 这个数组从此无人引用 ⇒ 泄漏
  t->q=q2;
  ```
- **泄漏量**：`(num+1) × sizeof(Timer_Entry)` = `(num+1) × 16` 字节 / 次。
  `test_timer.c:96` 用 `TimerInit(&tm2,2)`（队列容量 3）⇒ `3 × 16 = 48`，与 valgrind 报的 **48 B 精确吻合**。
- **生产可达性**：`ReceiverThread.c:11` 是 `TimerInit(&w->my_Timer,5000)` ⇒ 需 **5000 个并存连接**
  才会在 `TimerAlloc`（`Timer.c:53-55`）里 `elem_end==end` 触发扩容。以当前并发上限，**暂时被更早的墙挡住**；
  但一旦放并发（D-21）就会变成活跃泄漏。
- **处置**：**按 D-18 的决定——直接删掉 `TimerExpend`**，容量在 `TimerInit` 一次性给足
  （上界 = 最大并发连接数，是个**静态可算的数**，不需要动态扩容）。若暂时不想删，
  最小补丁是 `free(t->q->queue); free(t->q);` 并把容量改为 `2*(t->q->end-1)`。

### D-11 · L-3：`RouterHashExpend` 递归扩容 ⇒ 整张旧表失去引用（3,880 B/次）

- **状态**：仍存在（**单测 valgrind 实测 3,880 B**：16 B direct + 3,864 B indirect，分配点 `RouterHash.c:139/140`）
- **结构性可疑点（逐行核实）**：
  ```
  RouterHash.c:140  m = malloc(sizeof(Router_Hash)); RouterHashAlloc(m, PRIME_BUCKET_SIZES[nidex+1]);
  RouterHash.c:146  PRIME_BUCKET_SIZES_nidex++;                 // ← 全局下标，重哈希中会被重复推进
  RouterHash.c:156  RouterHashInsert(&m, m, url, func);         // ← 把"局部变量 m 的地址"当 hm 传进去
  RouterHash.c:217  if(h->elem_num > h->bu_num*2) RouterHashExpend(hm);   // ← 重哈希过程中可再次扩容（递归！）
  RouterHash.c:176  *h = m;                                     // ← 递归返回后覆盖外层指针
  ```
  递归扩容期间"新建的表"与"正在被 rehash 的表"指针互相覆盖，**必然有表结构失去引用** ⇒ 泄漏 + 逻辑错。
- **生产可达性**：`RouterInit` 用 `RouterHashInit(&e,2)` ⇒ 初始 **17 桶**；触发条件是 `elem_num > 34`。
  现网 `WebApp` 注册 **14 条路由** ⇒ **当前不触发**（卡在阈值 34 之下，很险）。
  `test_hash.c:46-48` 插 100 条路由就把这条路径打了出来。
- **处置**：**按 D-18 决定删除扩容**（路由表是启动期一次性注册的，运行期只读，本来就不需要扩容）：
  `RouterHashInit` 直接按预期路由数给足桶数（如 128 / 1024），`RouterHashInsert` 在
  `elem_num > bu_num*2` 时**返回 0 并打日志**，启动自检处断言"所有路由注册成功"。

### D-12 · BUG-16：`MemoryStackExpend` 漏 `free(leisure)` + 锁错对象 + 无返回值

- **状态**：仍存在（**死代码**，见下）
  ```c
  // MemoryPool/MemoryPool.c:25-42
  Memory_Stack* s=(Memory_Stack*)malloc(sizeof(Memory_Stack));
  MemoryStackInit(s,expend_num);
  ... 把旧栈元素搬到新栈 ...
  pthread_mutex_lock(&(s->mutex));   // ← 锁的是"新栈"，而被 free/替换的是旧栈 ⇒ 扩容期与并发使用者无互斥
  free(e->stack);                    // ← 没 free e->stack->leisure 数组
  e->stack=s;
  pthread_mutex_unlock(&(s->mutex));
  }                                  // ← 非 void 函数落到末尾，没有 return
  ```
- **可达性（诚实说明）**：唯一调用者是 `MemoryEntryExpend`（`MemoryPool.c:102`），
  而 `MemoryEntryExpend` 的唯一调用者是 `MemoryEntryAlloc`（`:110`）——**`MemoryEntryAlloc` 全库无调用者**
  ⇒ 当前是**死代码**。所以它现在的危害是"埋着没炸"，而不是活跃泄漏。
- **处置**：**按 D-18 一起删除**（不要修——扩容机制整体废弃，修了也是白修）。

### D-13 · `MMemoryStackPush` 没有上界检查（把"池损坏"升级成"堆越界写"）

- **状态**：仍存在（已核实 `MemoryPool.c:44-52`）
  ```c
  int MMemoryStackPush(Memory_Stack* s,int strip_serial)
  {
      pthread_mutex_lock(&(s->mutex));
      s->leisure[s->top]=strip_serial;   // ← 没有 if(s->top>=s->num) 检查
      s->top++;
      pthread_mutex_unlock(&(s->mutex));
      return 1;
  }
  ```
- **为什么必须补**：正常路径下 `Alloc` 弹一次、`Free` 压一次是平衡的，`top ≤ num` 成立；
  但一旦 D-01 / D-02 造成**重复归还**，`top` 就会超过 `num` ⇒ `leisure[num]` 越界写 ⇒ **堆破坏**。
  它是 D-02 的"最后一道防线"，也是把"静默池损坏"变成"确定性堆越界"的关键环节。
- **修法（3 行）**：
  ```c
  if(s==NULL||s->leisure==NULL||s->top>=s->num){ pthread_mutex_unlock(&(s->mutex)); return 0; }
  ```
  （并让调用方 `MemoryEntryFree` 检查返回值：返回 0 说明"重复归还"，应记日志而不是继续 `haded_num--`。）
- **验收**：探针断言「空闲栈溢出 `top>=num`」不再命中；压测 30 s 无越界。

### D-14 · L-5：`MemoryPoolFree` 校验失败直接 `return 0`，块**永久收不回来**

- **状态**：仍存在（已核实 `MemoryPool.c:209-213`）
  ```c
  int right_size=aim_entry->strip_size*aim_entry->strip_num*1024;
  if((char*)ptr<aim_entry->memory_strip||(char*)ptr+size-1>aim_entry->memory_strip+right_size) return 0;
  ```
- **危害**：这是**池内泄漏**——丢的是池内块、不是堆块，所以 **valgrind 看不见**；
  表现为"进程 RSS 不再增长，但可用块越来越少 ⇒ 最终全部 503"。
- **触发**：D-09 的单位错配、`ptr` 不属于该尺寸类、`size` 偏大（例如 `headers` 归还用了硬编码 `64512`）。
- **修法**：失败时**不要静默返回**——打日志（`ptr` / `size` / 命中尺寸类的地址区间）并累加一个
  "归还失败计数"暴露出来，压测时盯这个计数；同时修掉上游单位错配（D-09）。
- **验收**：压测 30 s 该计数恒为 0。

### D-15 · 暂存区扩容：拷贝长度用了"整块容量"而不是"已收数据量"

- **状态**：仍存在（已核实 `RequestTempPool.c:69-90`）
  ```c
  int nowsize=h->end-h->begin;            // = size*1024-1（整块容量-1，注意 Alloc 时已 -1）
  int expendsize=nowsize*2/1024+1;        // 数值上恰好 = 2*size KB，但写法绕
  ...
  memcpy(ptr,h->begin,nowsize);           // ← 拷的是整块容量，而不是"已收到的字节数"
  RequestTempPoolFree(h,pool);
  h->begin=(char*)ptr; h->end=h->begin+expendsize*1024-1;
  h->ptr_b=h->begin+ptr_b; h->ptr_e=h->begin+ptr_e;   // 偏移恢复，逻辑正确
  ```
- **问题**：① 拷贝长度比实际数据多（读到未初始化区域，浪费带宽、掩盖真实数据量）；
  ② 拷贝量与实际长度解耦后，将来任何"在 `ptr_e` 之后放临时数据"的改动都会出错；
  ③ `nowsize` 的 `-1` 偏移与 `end` 的定义耦合，极难维护。
- **修法（保留这个函数，只改 3 行）**：
  ```c
  int used=h->ptr_e-h->begin;             // 只拷有效数据
  memcpy(ptr,h->begin,used);
  ...
  h->end=h->begin+expendsize*1024-1;      // 保持不变
  ```
- **★ 为什么这个"扩容"可以留**：它是**唯一一个安全的扩容**——内容被复制到**新块**，
  旧块立即归还池，**没有任何外部指针指向旧块**（暂存区只被 `h->ptr_b` / `h->ptr_e` 以"偏移"方式引用）。
  这正是它与 §3 里那 4 个"不能留"的扩容的本质区别。
- **关联**：调用点 `RequestComeMain.c:84-88` 对 `hadBigest` 的处理是**正确的**（无法再扩大就 `FdClose(...,503)`）。

### D-16 · 新发现：定时器的三个语义问题（含一条**堆越界写**）

- **状态**：仍存在（本轮逐行核实 `PriorQueueTimer.c` + `Timer.c`）
- **(T1) `queue[0].fd` 被当"元素计数"用**（`PriorQueueTimer.c:23` 初始化 0、`:117` `++`、`:128/:140` `--`），
  而 `queue[0].time` 留在 `-2`。于是 `TimerExpend` 的 `2*(t->q->queue[0].fd)` 变成"按当前连接数分配容量"——
  语义完全错位（D-10）。**建议**：把计数单独放一个字段（如 `elem_num`），不要借用 `queue[0]`。
- **(T2) `TimerOvertime` 绕过了刚取到的 `t1.fd`**（`Timer.c:86-91`）：
  ```c
  Timer_Entry t1=PriorQueueTimerTop(t->q);
  int fd=t1.fd;                                          // ← 取到了正确的 fd
  while(...){ int e1=FdClose(w,t->q->queue[1].fd,408);   // ← 却用 queue[1].fd 关连接
  ```
  在 `TimerFree` 做过"末尾元素搬到 i"（`Timer.c:117-118`）之后，`queue[1]` 与堆顶未必同步 ⇒ **可能关错连接**。
  建议统一用 `t1.fd`。*（同时注意 `t1` 在循环里是重新取的，但 `FdClose` 用的永远是 `queue[1]`。）*
  **★ 编译器铁证（本轮 clean build 实测）**：
  `FdConnectManage/Timer/Timer.c:87:9: warning: variable 'fd' set but not used [-Wunused-but-set-variable]`
  —— 这就是"取到了 `t1.fd` 却从未使用"的机器证明，见 §6.4 第一张表。
- **(T3) ★ `TimerReset` 未命中时越界写**（`Timer.c:65-80`）：
  ```c
  int which=1;
  while(which>=1&&which<t->q->elem_end){ if(t->q->queue[which].fd==fd) break; which++; }
  t->q->queue[which].time=time(NULL);       // ← 未命中时 which==elem_end，仍无条件写
  ```
  `queue` 只有 `end` 个元素（下标 `0 .. end-1`），**若队列恰好满（`elem_end==end`）就会写 `queue[end]` ⇒ 堆越界写**。
  触发链：`TimerAlloc` 曾失败过的连接（例如 D-10 容量算错导致 `PriorQueueTimerInsert` 返回 0 ⇒ `TimerAlloc` 返回 -1）
  ⇒ 该 fd 没有计时器条目 ⇒ 之后每次 `ReveiverThreadMain:57` 的 `TimerReset` 都命中"未找到"分支。
  修法：`if(which>=t->q->elem_end) return 0;`（或改为"未命中则插入"）。
- **附带待确认项（不建议现在动）**：`TimerFree`（`:104-126`）的"末尾元素覆盖 i + `PriorQueueTimerDown(i)`"组合，
  在 i 位于上滤路径时是否会破坏堆序，**需要专项单测确认**；若确认有问题，建议整表重写为**时间轮**。

### D-17 · 两个"并列上限"（其中一个是新发现的**必崩路径**）

- **状态**：仍存在（三项均已核实）

**(a) 每个 Worker 自带的 `http_pool` 只有 10 块**（与全局 `http_pool` 差 40 倍）
```c
// Threads/WorkerThread.c:78
MemoryPoolInit(&((*profes)->http_pool),6,6,10,10);   // 6 个尺寸类(1/2/4/8/16/32KB)，每类只有 10 块
```
对比：`ReceiverThread.c:8` 的全局 `http_pool` 是 `6,6,500,400`（每类 400 块）。
而 **请求分析对象是从这个只有 10 块的池里出的**（`WorkerThread.c:129`、`:205`）⇒ 2 个 Worker 共 20 块
就是"同时在解析的请求"的硬上限，与全局 400 块形成**两个数量级不一致的上限**。

**(b) `ed_store_blocknum=100` 与 send 池 400 块不匹配**
```c
// RequestCome/RequestComeMain/RequestComeMain.c:10-11
//如果在途包比send池大则先不继续发包，防止send池循环覆盖未发数据
if(fd_ob->pack_in_path>=ed_store_blocknum) return 1;      // ed_store_blocknum 默认 100
```
注释的本意是"每连接在途包 ≤ send 池块数"，但 send 池（`store_area`，`10,6,500,400`）有 400 块，
每连接却按 100 算 ⇒ **4 条连接打满 100 就吃光整个 8 KB 类**（对应压测报告 §1.7 实测的"~400 那道墙"）。
两者必须由**启动自检**强制成同一套数字（见 D-21）。

**(c) ★ 新发现的必崩路径：`RequestAnalysisInit` 失败后 `memcpy` 到未初始化指针**
```c
// RequestAndResponse/... RequestAnalysiser.c:15-22
int RequestAnalysisInit(Request_Analysiser** h,Memory_Pool* pool,int h_size)
{
    int e0=MemoryPoolAlloc(pool,(h_size/1024)+1,&ptr,&notfull);   // ← e0 未使用（= D-25）
    if(notfull==0)  return -1;                                    // ← 失败时 *h 完全没被赋值
    (*h)=ptr;
...
// Threads/WorkerThread.c:112-113（WorkerMain 内）
    Request_Analysiser* h;                                        // ← 未初始化
    RequestGet(profes,Http,&h,Size,&Error_reason,W);
// Threads/WorkerThread.c:205-207（RequestGet 内）
    RequestAnalysisInit(h,profes->http_pool,allocsize);           // ← 返回值没检查！
    memcpy((*h)->ptr,ptr,size);                                   // ★ (*h) 是栈上垃圾 ⇒ 野指针解引用
```
- **触发**：`http_pool` 的相应尺寸类耗尽（(a) 只给了 10 块）⇒ `RequestAnalysisInit` 返回 `-1`
  ⇒ `(*h)` 仍是**未初始化的栈垃圾** ⇒ `memcpy` 写野地址 ⇒ **SIGSEGV**。
  这比 D-04 更容易命中，因为阈值只有 10 块。
- **修法（3 处，都是"失败要显式置空 + 调用方要检查"）**：
  ```c
  // RequestAnalysiser.c:20
  if(notfull==0){ *h=NULL; return -1; }
  // WorkerThread.c:205
  if(RequestAnalysisInit(h,profes->http_pool,allocsize)!=1){ *error_reason=503; return 1; }
  // WorkerThread.c:112
  Request_Analysiser* h=NULL;
  ```
- **验收**：把 `http_pool` 的块数临时改成 1 跑并发，应全部回 503 且不崩。
- **关联**：与 D-04 / D-06 是**同一个反模式的三次重复**——"失败只设错误码、不置空指针、调用方不检查"。
  建议修完这三处后**全库搜一遍** `notfull==0`，把同类写法一次性收干净。

---

## 3. S2 —— 结构性：**扩容机制作废**后的处置（本轮决定）

### D-18 · 全库 5 条扩容路径的逐点处置

**决定**：不再采用"扩容"方案（一扩容就要把数据和指针整体搬迁，不可行）。
下面是全库 `Expend` 的**完整清单**（`grep -rn Expend` 核实），逐条给"能不能搬 + 怎么办"：

| # | 扩容点 | 搬的是什么 | 搬迁会不会让**已发出的指针**失效 | 处置 |
|---|---|---|---|---|
| E-1 | `MemoryEntryExpend`<br>`MemoryPool.c:85-106` | `memcpy` **整个 strip** 到新地址，然后 `free` 旧 strip | **会，而且是致命的**：已发给使用者的每一个块指针**全部变悬垂**（use-after-free） | **删**（连同 E-2、以及死代码 `MemoryEntryAlloc`） |
| E-2 | `MemoryStackExpend`<br>`MemoryPool.c:25-42` | 空闲 serial 数组（纯 `int`） | 不会 | **删**（随 E-1；"栈满"改为拒绝，见 D-13） |
| E-3 | `TimerExpend`<br>`Timer.c:16-31` | `Timer_Entry` 数组（fd + 时间戳） | 不会 | **删**；容量 = **最大并发连接数**（静态可算），`TimerInit` 一次给足 |
| E-4 | `RouterHashExpend`<br>`RouterHash.c:136-182` | 桶数组 + 链表 + **重哈希** | **会**：`:156` 传局部变量地址、`:217` 重哈希中递归扩容、`:176` `*h=m` 覆盖外层指针 | **删**；路由表**运行期只读**，`Init` 一次给足桶数，`Insert` 满即**拒绝并报错** |
| E-5 | `RequestTempPoolExpend`<br>`RequestTempPool.c:69-90` | 换更大的尺寸类块 + `memcpy` 内容 | 不会：外部只以**偏移**（`ptr_b`/`ptr_e`）引用，旧块立即归还池 | **保留**（唯一安全的扩容），只修拷贝长度（D-15） |

**作废扩容后的三条替代原则（建议写进代码注释与自检）：**

1. **容量在启动时按"静态上界"一次给足**——连接数上界、路由数、每连接在途包数都是可以在启动前算出来的数，
   不需要运行期长大；
2. **所有"申请不到"的分支必须显式失败**（返回 `0/-1` + 日志 + 回 503），
   绝不允许"静默扩容"或"拿着空指针继续跑"（这正是 D-04 / D-17c 的成因）；
3. **上界之间的一致性由启动自检保证**（见 D-21），人手不许自己心算（D-17(b) 就是心算错的例子）。

**清理顺序建议**：先删 E-1/E-2（纯死代码，零风险）→ 再删 E-3 → 最后删 E-4（要连带改 `RouterInit` 的桶数）。

### D-19 · `MemoryEntryExpend`：一旦被调用就会静默毁掉整个池（现在是死代码，但必须删）

- **状态**：**死代码**（已核实：`MemoryEntryAlloc` 全库只有定义、无调用者）
  ```c
  // MemoryPool/MemoryPool.c:85-106
  char* strip=(char*)malloc(expend_num*strip_size*1024);
  memcpy(strip,e->memory_strip,total_size);   // ← 整池搬迁
  e->memory_strip=strip;                      // ★ 地址变了：之前发出去的每个块指针全部悬垂
  free(p);
  ```
- **为什么它必须消失（而不是"留着不用"）**：
  ① 它是"谁手滑调用一次就静默毁掉整个池"的雷（UB 现场极难定位）；
  ② 它与本项目的**设计本意相冲突**——`MemoryPoolAlloc:177-181` 对"超出最大尺寸类"的处理是
     `malloc` 兜底 + `*notfull=1`，说明**池满的语义本来就是"拒绝/兜底"，从来不是"扩容"**；
  ③ 它和 E-2 串在一起，还带着 D-12 的漏 free + 锁错对象。
- **删法**：删除 `MemoryEntryExpend`（`:85-106`）、`MemoryStackExpend`（`:25-42`）、
  `MemoryEntryAlloc`（`:108-119`）三个函数及头文件声明；
  **保留** `MemoryEntryAllocTwo`（在产路径，池满时 `*notfull=0` ⇒ 上层回 503）。
- **验收**：`grep -rn "Expend" --include=*.c .` 只剩 E-3/E-4/E-5 的调用点，
  再随 D-10 / D-11 清完即为**零**；`make -j4` 与 `make -C test run` 无回归。

### D-20 · 生命周期：全库只有 2 个 `Destory` ⇒ 无销毁接口、无优雅关闭路径

- **状态**：仍存在（`grep` 核实）
  ```
  全库 Destory/Destroy 定义只有两处：
    FdConnectResource/RequestTempPool/RequestTempPool.c:26   RequestTempPoolDestory
    FdConnectResource/SendPointerPool/SendPointerPool.c:78   SendPointerPoolDestory
  ```
  以下对象**只能分配、永不释放**：`Memory_Pool` / `Memory_Entry` / `Memory_Stack` / `Worker_Queue` /
  `Task_Queue` / `Receiver_Queue` / `Timer` / `Prior_Queue_Timer` / `Fd_Table` / `Router_Hash` /
  `Worker_Thread` / `Receiver_Thread`。
- **实测**：valgrind `still reachable: 84,268,672 bytes in 172 blocks`。
  按源码核算构成（这部分**不是泄漏**，是启动即常驻的设计）：
  ```
  Receiver  http_pool (6,6,500,400)   = 400 × (1+2+4+8+16+32)KB  ≈ 24.6 MB
  Receiver  store_area(10,6,500,400)  = 400 × 63KB               ≈ 24.6 MB
  Worker×2  txt_pool  (7,7,200,120)   = 120 × 127KB × 2          ≈ 29.8 MB
                                       小计                          ≈ 79 MB（+ 栈数组零头 ≈ 84 MB）
  ```
- **三条实际影响**：
  1. 部署内存要按 **~85 MB / 进程** 起算（这个数字本身不坏，写进 README 即可）；
  2. **没有优雅关闭路径**——收到信号只能 `kill -9`，无法在测试里验证"释放是否完整"；
  3. **单元测试里"框架无 destroy API"的 `definitely lost` 永远清不掉**
     （上一轮实测：`test_memory_pool` 3,032 / 15,320 B、`test_queues` 152 / 256 / 296 B 都属这一类），
     这会让"泄漏扫描"长期带噪音、掩盖真问题。
- **建议（按成本从低到高，不必一次做完）**：
  1. 先给**纯堆结构**加销毁：`TimerDestory`（free `q->queue` + `q`）、`FdTableDestory`、
     `RouterHashDestory` —— 每个都是十几行；
  2. 再加主进程的退出路径（`SIGINT/SIGTERM` → 尽力释放 + 打印池使用统计），
     **即使什么都不释放，有退出路径就能跑 valgrind 的完整报告**；
  3. `MemoryPoolDestory` 放最后（要遍历每个 `Memory_Entry` 依次 free `memory_strip`、
     `stack->leisure`、`stack`，容易写错）。

### D-21 · 容量自检与统一限流（`RATELIMIT_DESIGN.md` 的方案尚未落地）

- **状态**：未实施（设计文档已就绪）
- **现状**：系统里有**至少 7 个互不相干、全靠人脑记住的数字**：

  | 数字 | 位置（实施前请用下面的 grep 复核一遍） |
  |---|---|
  | `listen` backlog = 100 | `StaterMain/StarterMain.c` |
  | `taskQueue` 容量 = 100 | `TasKQueueInit` 调用点 |
  | `memory_queue` 容量 = 100 | `WorkerThread.c:76` `WorkerQueueInit(...,100)` |
  | `Thing_queue`（接收队列）容量 = 100 | `ReceiverThread.c:10` `ReceiverQueueInit(...,100)` |
  | `ed_store_blocknum` = 100 | `ReveiverThreadMain` 入参，源头在 `main.c`/`StarterMain.c` |
  | send 池 `store_area` = 400 块 | `ReceiverThread.c:9` |
  | 定时器容量 = 5000 | `ReceiverThread.c:11` |
- **风险**：**任何两处数字之间的不一致都会形成一道墙**（D-17(b) 就是活例子：`100` 与 `400` 的关系被写错）。
  这类错误在压测里表现为"某个尺寸类的块突然全部耗尽 → 大批 503"，而**代码里没有任何地方会报错**。
- **方案**（设计已重新落到仓库内：**`test/RATELIMIT_DESIGN.md`**）：
  新模块 `Limit/GlobalLimit/`（根 Makefile 是 `SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)`，
  新目录会被自动收编 ⇒ **零 Makefile 改动**，已核实）+ 在 `GlobalResourceInit` 里做**启动自检**，
  **违反不变式直接拒绝启动**——把"数字之间的一致性"交给机器，不再靠人记事。
  完整的不变量清单、"当前配置代入"结果与实施顺序见 **`test/RATELIMIT_DESIGN.md`**。
  （结论预告：**当前配置 I4 / I5 / I6 三条不成立、I7 无余量**，
  必须先修好 D-17(a) 的每 Worker 10 块与 D-17(b) 的 `ed_store_blocknum` 与池容量关系，见 D-17。）
- **顺序（硬性）**：**必须在 S0 / S1 全部修完之后再做**。限流会把并发推高，
  在 D-01 / D-02 / D-03 / D-04 / D-08 / D-17c 没修之前上，等于把"偶发崩溃"变成"必现崩溃"。
- **实施前先跑一遍**（把全部常量抓齐）：
  ```bash
  grep -rn "listen(\|QueueInit(\|MemoryPoolInit(\|TimerInit(\|ed_store_blocknum" --include=*.c . | grep -v '^./test/'
  ```

---

### D-30 · 线程入口参数指向调用者栈帧（现在靠"永不返回"侥幸成立）

- **状态**：仍存在（已核实 `StaterMain/StarterMain.c:60-84`）。
- **现象**：
  ```c
  int StartThread(int fd){
      pthread_t tid2;                                  // ← 两个 Worker 复用同一个 tid2
      Worker_Thread* p1; WorkerThreadInit(&p1,1);      // Worker 1
      main_t2 m5 = {.p = p1};                          // ← 栈上局部变量
      pthread_create(&tid2, NULL, func2, &m5);         // ← 把栈地址交给新线程
      Worker_Thread* p2; WorkerThreadInit(&p2,2);      // Worker 2
      main_t2 m6 = {.p = p2};
      pthread_create(&tid2, NULL, func2, &m6);         // ← tid2 被覆盖，且同样是栈地址
      Receiver_Thread* w1; ReceiverThreadInit(&w1, fd, 1);
      main_t m1 = {.fd = fd, .time = 60, .w = w1, .ed_store_blocknum = 100};
      ReveiverThreadMain(m1.w, m1.fd, m1.time, m1.ed_store_blocknum);  // ← 本线程阻塞在这里
  }
  ```
  线程体是 `while(1) WorkerMain(m->p);`，而 `m` 指向 **`StartThread` 的栈帧**。
- **为什么现在不炸**：`StartThread` 最后调用的 `ReveiverThreadMain` **永不返回**（Receiver 事件循环是
  `while(1)`），那个栈帧因此一直有效 —— 这是**"靠不返回侥幸成立"，不是设计**。
- **什么时候必炸**：**只要给 `ReveiverThreadMain` 加上退出条件**（也就是做 **D-20 的优雅关闭**），
  `StartThread` 就会返回 ⇒ 栈帧失效 ⇒ **两个 Worker 线程的 `m->p` 立即变成野指针**。
  ⇒ **D-30 不做则已，一做 D-20 就必然踩到它。**
- **附带问题**：`pthread_create(&tid2, ...)` 两次写入同一个变量 ⇒ 第一个 Worker 的句柄丢失。
  现在没人 `join` 所以无感，但**做关闭时必须能 join**，所以要改成数组。
- **修法**（与 D-20 同一轮，约 6 行）：
  ```c
  main_t2* m5=(main_t2*)malloc(sizeof(main_t2)); m5->p=p1;   // 或改成静态数组
  pthread_t tid[2];
  pthread_create(&tid[0], NULL, func2, m5);
  // 第二个 Worker 同理，用 m6 / tid[1]
  ```
- **验收**：优雅关闭时两个 Worker 能安全退出并被 `join`；
  **关闭流程走完后主进程能正常 `return`**（现在只要 `StartThread` 一返回就会崩）。
- **关联**：**D-20**（优雅关闭）、**D-05**（关连接时的回收）。

---

## 4. S3 —— 性能（3 条）

### D-22 · OPT-01：缺 `TCP_NODELAY` ⇒ 静态页 QPS **1,049 → 11,777（11.2×）**

- **状态**：**仍未加**（已核实：全库 `grep TCP_NODELAY` 零命中；`FdConnectOrClose.c:16-20` 里
  `accept` 拿到 `client_fd` 后直接进 `FdTableInsert`，中间没有任何 `setsockopt`）
- **实测代价**（压测报告 OPT-01）：静态页 QPS **1,049 → 11,777**，P50 延迟降到 **1/24.9**。
  根因是 Nagle 算法与延迟 ACK 互相等待（每次响应头/体分两次 `write`，必然触发）。
- **修法（2 行，无任何风险）**：
  ```c
  // FdConnectOrClose/FdConnectOrClose.c:16 拿到 client_fd 之后
  int one=1;
  setsockopt(client_fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
  ```
- **为什么建议放到 S0 之后做**：它是**唯一一个会把负载整体放大一个数量级的改动**——
  同样的 `wrk -c200` 在修好前是"每连接 1 个请求"，修好后单位时间内的请求数涨 10 倍，
  会更快撞上 D-04 / D-08 / D-17c 那些"偶发"崩溃点。
- **验收**：同一台机器、同一命令，QPS 应复现 10× 量级提升；`ss -i` 中 `NODELAY` 生效。

### D-23 · OPT-02：`Connection: close` 硬编码，没有 keep-alive

- **状态**：仍存在（已核实 `FdConnectOrClose.c:95` 与错误响应构造函数里都是硬编码 `Connection: close`）
- **代价**：每个请求一次 TCP 三次握手 + `TIME_WAIT`；
  同时**把 D-05（12 B/连接）按请求数放大**——并发不变、请求数 ×100 ⇒ 泄漏 ×100。
- **★ 前置条件（顺序不能颠倒）**：
  1. 必须先修 **D-05**（否则泄漏按请求数增长）；
  2. 必须先修 **D-01 / D-03 / D-08**（keep-alive 会让**同一个 fd 反复走 `FdClose` / `FdTableFind` 路径**，
     把现在的"偶发"直接变成"必现"）；
  3. 需要给状态机加"请求边界复位"：`RequestTempPool` 的 `ptr_b/ptr_e` 归零 + `state_machine` 状态复位
     （**现在没有这个复位入口**，这是 keep-alive 的主要工作量）；
  4. 还要决定是否支持 pipelining（建议**先不支持**，串行处理完一个再读下一个）。
- **建议**：**放到最后做**。收益是"每请求省一次握手"，风险是"动连接生命周期"，
  性价比远低于 D-22。

### D-24 · 未归因项（**在搞清楚之前不要盲改**）

- **P90 残留 40 ms**：`/hello` 已从 1.19 s 降到 40 ms，但离"1 ms 量级"还差两个数量级，
  当前**没有定位到具体环节**（是 epoll_wait 批次调度？是 `TimerReset` 的队列扫描？是日志？）。
- **QPS 随并发上升**：说明系统不是 CPU 瓶颈，而是"每次请求的固定开销"主导 —— 这条线索本身就指向
  D-22（Nagle 造成的固定等待）与 accept 路径的串行开销。
- **`FdConnect` 路径**：accept 循环 + 每连接 3 次挂载（`FdTableInsert` / `EpollReadMount` / `TimerAlloc`）
  + 2 次池分配，全部在**单个 Receiver 线程**里串行。
- **建议的定位手段（不改代码逻辑，只打点）**：
  ```bash
  perf record -g --call-graph dwarf -p $(pgrep -f c_web) -- sleep 20 && perf report --stdio | head -50
  ```
  以及"分阶段计时"：把 `epoll_wait` 返回时间戳、`RequestComeMain` 结束时间戳、`write` 结束时间戳
  各自累加到一个全局统计里，压测后打印占比。**先量出来，再决定改哪里。**

### D-32 · 任务队列取消阻塞后，业务线程失去等待机制 ⇒ 空闲 CPU 100%/核

- **状态**：仍存在（已实测）。**这是 D-31 的遗留项**：D-31 修的是"计数与指针不同步"，
  D-32 是"去掉信号量后没人负责让出 CPU"。
- **位置**：`Threads/WorkerThread.c`（取任务侧的循环）＋ `Queue/TaskQueue/TaskQueue.c`（已无等待语义）
- **实测**：服务器启动后**不发任何请求**，`top -b -n 2 -d 1 -p <pid>` 采样 `%CPU = 197 / 200`
  （`NLWP=3`，2 核 VM 被 2 个 Worker 打满）；`ps -o cputime` 逐秒线性增长。
- **原因**：`TasKQueueTopAndPop` 空队列立即 `return`，`WorkerMain` 只能死循环重试 ⇒ 忙轮询。
- **⚠️ 不能改回 `sem_wait` 阻塞整个 Worker**：那正是 D-31 描述的互等链（收发线程阻塞在满队列
  ⇒ epoll 循环停摆 ⇒ 全站卡死）。可行的等待策略（任选其一，都保持"收发线程永不阻塞"）：
  1. **退避自旋**：空队列时 `nanosleep(50~200 µs)`（1 行改动；CPU 近 0，延迟可控）；
  2. **条件变量**：`push` 成功后 `pthread_cond_signal`，Worker 用 `pthread_cond_timedwait` 等
     （零忙等；push 侧仍非阻塞，不构成互等）；
  3. **信号量只做唤醒**：`sem_post` 仅用于唤醒、`sem_timedwait` 带超时，
     **不把信号量当容量闸门**（闸门仍由锁内 `num` 判定）—— 等价于 2 但改动更小。
- **验收**：空闲 60 s 进程 CPU < 5%；`bash test/website_test.sh` 仍 27/27；P99 不回退。
- **关联**：D-31（同一处改动的另一半）。

---

## 5. S4 —— 工程与清理（5 条）

### D-25 · BUG-08：`RequestAnalysiser.c:19` 的分配结果无人检查

- **状态**：仍存在（已核实 `RequestAnalysiser.c:19`：`int e0=MemoryPoolAlloc(...)`，`e0` 全程未使用）
- **注意**：这个函数**已经**检查了 `notfull==0`（`:20`），所以 `e0` 的实际作用只是"想在 `malloc` 兜底路径
  与"池分配"路径上做区分"——但它没做，白留一个 `-Wunused`。
- **修法**：随 **D-17c** 一起改：`if(MemoryPoolAlloc(...)!=1||notfull==0){ *h=NULL; return -1; }`，删掉 `e0`。

### D-26 · 残余编译告警（建议以"`-Wall -Wextra` 零告警"作为退出标准）

- **状态**：仍存在。**本轮 clean build 实测：`-Wall` 下 17 条告警**（完整清单见 **§6.4**），
  其中 9 条是 `-Wreturn-type`、7 条 `-Wunused-variable`、1 条 `-Wunused-but-set-variable`。
  最关键的两条**不是噪音，而是真缺陷的编译器证据**：
  - `Timer.c:87` `variable 'fd' set but not used` ⇒ **D-16 (T2)**（取到 `t1.fd` 却用 `queue[1].fd` 关连接）；
  - `MemoryPool.c:42` `control reaches end of non-void function` ⇒ **D-12**（`MemoryStackExpend` 无 `return`）。
- **修法**：不要一次全清——**每修一条 D-xx 就顺手清掉它相关的告警**，最后再统一加 `-Wextra` 收尾。
  加 `-Wextra` 时优先处理 `unused-parameter`、`sign-compare` 两类（它们的提示往往能带出真 bug）。

### D-27 · CI 护栏：`make -C test run` 之外还缺三层

- **状态**：未实施（现状 `test/run_tests.sh` 只跑单元测试，286 PASS，**没有** ASan、并发压测与不变式断言）。
  本轮已先补上"并发/不变式探针"这一层（`test/src/test_taskqueue_race.c`，见 D-31/D-29），
  仍缺 **ASan 并发回归**与"池/队列不变式钩子"两层。
- **建议补三层（从便宜到贵）**：
  1. **ASan 并发回归**（能抓到 D-01/D-02 那条链上的 `strcat-param-overlap` 与 use-after-free）：
     ```bash
     make clean && make CFLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread" -j4
     ./c_web & sleep 1 && wrk -t2 -c100 -d30s http://127.0.0.1:8080/hello ; kill %1
     ```
     **判定**：ASan 无 `ERROR: AddressSanitizer` / `SUMMARY` 输出。
  2. **探针断言**（最快、最直接；原 `/tmp` 的探针副本已随清空丢失，
     按 **D-29** 的四条不变式重写一个即可，约 60 行）：
     空弹出计数 = 0、`notfull ≤ 容量`、空闲栈 `top ≤ num`、归还失败计数 = 0。
  3. **泄漏门限**（`definitely lost == 0`）：
     ```bash
     valgrind --leak-check=full --show-leak-kinds=definite --error-exitcode=1 ./c_web ...
     ```
- **为什么值得马上做**：本清单里 S0 的 8 条**全部**都能被上面第 1、2 层直接抓到，
  代价却只是十几行 shell + 一个已有探针文件。

### D-28 · 测试自身的泄漏（**先别加 suppression**）

- **状态**：仍存在（上一轮实测：`test_response` 263,696 B、`test_hash` 8,xxx B）
- **归因**：`test_hash` 那部分**正是 D-11**（`RouterHashExpend` 丢表）；`test_response` 是"用真实池路径造响应"
  带来的框架常驻内存（D-20 家族）。
- **建议顺序**：**先修 D-11 / D-05 / D-10**，再看剩下多少；
  **不要**用 suppression 文件把噪音盖掉——盖掉之后真正的泄漏也看不见了。

### D-29 · 池/队列的"不变式断言"（压测报告 10.2 的 4 条尚未落地）

- **状态**：未实施
- **四条不变式**（建议放进 `test/src/test_invariants.c`，用现成的 `assert` 风格）：
  1. 队列：`0 ≤ num ≤ blocknum` 且 `notfull == blocknum - num`（**直接抓 D-01**）；
  2. 空闲栈：`0 ≤ top ≤ num`（**直接抓 D-02/D-13**）；
  3. 内存池：`haded_num ≤ strip_num` 且 `top + haded_num == strip_num`（**直接抓重复归还**）；
  4. 分配/归还计数平衡：`alloc_count - free_count == 在用块数`。
- **前置**：需要给 `Memory_Pool` / `Worker_Queue` 加**只读统计接口**（现在外部拿不到 `num` / `top`），
  这也是 D-27 第 2 层探针所依赖的。**建议先加统计接口，再写断言。**

---

## 6. 附录

### 6.1 一键复跑命令（每次改完都跑这一套）

```bash
cd /home/yaoxianglongde/c_code/c_web

# ① 构建：要求 rc=0，且告警数不上升（当前基线 17 条，见 §6.4）
make clean && make -j4 2>&1 | tee /tmp/build.log
echo "build_rc=${PIPESTATUS[0]}"; grep -c 'warning:' /tmp/build.log

# ② 单元测试：要求最后一行是 "==== 全部单元测试通过 ===="（286 PASS / 0 FAIL / 0 HANG）
make -C test run

# ③ 单个用例的泄漏扫描（用例二进制在 test/build/ 下）
valgrind --leak-check=full --show-leak-kinds=definite ./test/build/test_timer
valgrind --leak-check=full --show-leak-kinds=definite ./test/build/test_hash
valgrind --leak-check=full --show-leak-kinds=definite ./test/build/test_temp_pool

# ④ 启动 + 并发压测（端口以 main.c 的 WebStater 实参为准）
./main & sleep 1
wrk -t2 -c200 -d30s --latency http://127.0.0.1:5343/hello    # 纯动态路径
wrk -t2 -c50  -d10s --latency http://127.0.0.1:5343/         # 静态 sendfile 路径
kill %1

# ⑤ 线程泄漏（仓库自带 target）
make valgrind

# ⑥ ASan 并发回归：要求无 "ERROR: AddressSanitizer"
make clean && make CFLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread" -j4
#    然后重复 ④ 的压测命令

# ⑦ 网站端到端（27 条 curl 断言，含 10MB 大文件下载）
make -C test website
```

### 6.2 证据索引（每条结论的来源，便于回查）

| 编号 | 主要证据 | 出处 |
|---|---|---|
| D-01 / D-02 | 35 s 内 18,748 次空弹出 post、信号量 100→101→102…；ASan `strcat-param-overlap` ×55；本轮独立探针（容量 3）5 次空 pop 后 `notfull` 3→8 且 `num=5>3` | `STRESS_TEST_REPORT.md` §4.4 / §4.5 + 本轮探针 |
| D-03 / D-07 / D-08 | 源码逐行；编译器 `unused variable` 告警（`ReceiverThread.c:25/40/54/59`） | 本轮核实 + §6.4 |
| D-04 | `WorkerThread.c:8-23` 逐行；`txt_pool` 的 64 KB 类仅 120 块 | 本轮核实 |
| D-05 | 进程级 valgrind：`definitely lost: 2,400 bytes in 200 blocks`（12 B/连接），调用栈闭合到 `StateMachineInit`；`test_temp_pool` 同族 12 B | 泄漏专项 + 本轮 `test_temp_pool` 复跑 |
| D-09 | `MemoryPool.c:173` vs `:197` 单位对照 + 10 个调用点逐一比对 | 本轮核实 |
| D-10 | `test_timer` valgrind 48 B = (2+1)×16，与 `TimerInit(&tm2,2)` 精确吻合 | 泄漏专项 + `test_timer.c:96` |
| D-11 | `test_hash` valgrind 3,880 B（16 direct + 3,864 indirect），分配点 `RouterHash.c:139/140` | 泄漏专项 + `test_hash.c:46-48` |
| D-16 | **编译器告警 `Timer.c:87 set but not used`** + `Timer.c:65-91` 逐行 | §6.4 + 本轮核实 |
| D-17(c) | `RequestAnalysiser.c:15-22`（失败不置空）→ `WorkerThread.c:205`（不检查）→ `:207`（解引用）完整链条 | 本轮核实 |
| D-20 | valgrind `still reachable: 84,268,672 bytes in 172 blocks` + 源码核算 ≈79 MB 池本体 | 泄漏专项 |
| D-22 | A/B 对照：QPS 1,049 → 11,777、P50 43.60 ms → 1.75 ms | `STRESS_TEST_REPORT.md` §8.1 |
| D-23 | `FdConnectOrClose.c:95` 硬编码 `Connection: close` | 本轮核实 |
| D-25 / D-26 | 编译器告警（clean build 17 条） | §6.4 |
| D-30 | `StarterMain.c:60-84` 逐行：`main_t2 m5/m6` 为栈变量却被 `pthread_create` 取址；`ReveiverThreadMain` 阻塞不返回 | 本轮核实 |
| D-31 | 探针 `test/src/test_taskqueue_race.c`（3 次运行统计一致）+ TSAN `data race TaskQueue.c:44 vs :81` + gdb 崩溃栈 `WorkerThread.c:141` + `website_test.sh` 修复前 6/21 且段错误 → 修复后 27/27 | **仓库内可复跑**（`make -C test run`、`bash test/website_test.sh`） |
| D-32 | `top -b -n 2 -d 1 -p <pid>` 空闲采样 `%CPU=197/200`、`NLWP=3` | 本轮实测 |
| D-19 / D-18 | `grep -rn Expend` 全库 **5 处**（Timer / RequestTempPool / MemoryStack / MemoryEntry / RouterHash），**无 `FdTableExpend`**；`MemoryEntryAlloc` 无调用者 | 本轮核实 |

### 6.3 修复进度打勾表（照这个顺序做，边做边勾）

#### 第 0 批 · ✅ 本轮已完成（任务队列 CAS 改造的善后）
- [x] **D-31** 把满/空判定与计数增减收进同一把互斥区（`TaskQueue.c` 的 `push`/`pop`），
      删掉 4 处 CAS 与重复 `num++/--`
      验收：探针 `-O0` 与 TSAN 双通过（**23 PASS / 0 FAIL**，TSAN 0 报告）；
      `website_test.sh` **27/27**；`make -C test run` 全绿
- [x] **D-31 配套** 新增 `test/src/test_taskqueue_race.c`（注册进 `TESTS`）；
      `test_queues.c` 4 条旧"阻塞语义"断言改写为"非阻塞背压语义"

#### 第 1 批 · 止住"内存池被毁"这条主链（共 ~5 行）
- [ ] **D-01** 队列空弹出不再归还容量（`WorkerQueue.c:72` 搬进 `if` 块，1 行）
      验收：空弹出计数 = 0、`notfull ≤ 容量`、`wrk -c200 -d30s` 3 轮不崩
- [ ] **D-13** `MMemoryStackPush` 加上界检查（`MemoryPool.c:44-52`，3 行）
      验收：不再出现空闲栈溢出；调用方 `MemoryEntryFree` 检查返回值
- [ ] **D-02** `MemoryEntryFree` 加"是否已分配"校验（`MemoryPool.c:136-146`，位图或 `top+haded_num==strip_num` 断言）
      验收：压测 30 s 无"释放了不在分配状态的块"

#### 第 2 批 · 五条必崩路径（全是"检查返回值 + 提前 return"）
- [ ] **D-03** `FdClose` 检查 `FdTableFind`（`FdConnectOrClose.c:50-56`，1 行）
- [ ] **D-04** `RspHeaderAlloc` 池满立即 `return 0`（`WorkerThread.c:8-23`，2 行）+ 调用点 `:107` 检查
- [ ] **D-17(c)** `RequestAnalysisInit` 失败置 `*h=NULL` + 调用点检查 + `h` 初始化（3 处）
- [ ] **D-08** 三处 `FdTableFind` 加守卫（`RequestComeMain.c:8`、`ResponseSendMain.c:89`、`:36`）
      验收：低超时(1s) + 高并发压测 30 s 不崩（现在必崩）
- [ ] **D-07** 4 处返回值检查（含 `ReceiverThread.c:25` 改为 `return TasKQueuePush(...)`）
      验收：`taskQueue` 调小后客户端收到 503 而不是干等超时

#### 第 3 批 · 泄漏与 UB
- [ ] **D-05** `StateMachineDestory` + `RequestTempPoolDestory` 里回收（3 处）
      验收：200 连接 valgrind `definitely lost` 由 2,400 B → **0**
- [ ] **D-06** `char* C=NULL;` + 503 分支显式置空（2 行）

#### 第 4 批 · 单位 / 归还 / 越界 / 拷贝长度
- [ ] **D-09** 统一为字节（并加断言）
- [ ] **D-14** `MemoryPoolFree` 失败不再静默（加日志 + 计数）
- [ ] **D-16** 修 T1/T2/T3（T3 `TimerReset` 未命中即 `return 0`，防越界写）
- [ ] **D-15** 暂存区扩容只拷有效数据（3 行）

#### 第 5 批 · 扩容机制作废（专门一轮）
- [ ] **D-18/D-19** 删 E-1/E-2 + `MemoryEntryAlloc`（死代码，零风险）
- [ ] **D-10** 删 E-3（`TimerExpend`），容量按最大并发一次性给足 → `test_timer` 48 B 归零
- [ ] **D-11** 删 E-4（`RouterHashExpend`），`Init` 给足桶数 → `test_hash` 3,880 B 归零
- [ ] **D-12** 随 E-1/E-2 一起删除
      验收：`grep -rn Expend` 只剩 E-5（暂存区）

#### 第 6 批 · 性能
- [ ] **D-22** 加 `TCP_NODELAY`（2 行，零风险）
      验收：QPS 复现 10× 量级提升
- [ ] **D-24** 先 `perf` + 分阶段计时定位 P90 40 ms，**定位后再改**
- [ ] **D-23** keep-alive（**放最后**，前置：D-05 + D-01/D-03/D-08 已修）

#### 末批 · 容量与护栏（必须等前面稳了）
- [ ] **D-17(a)(b)** 把两个不一致的上限改对（`http_pool` 10 块 → 与全局一致；`ed_store_blocknum` 与 send 池对齐）
- [ ] **D-21** 启动自检 + 限流（`test/RATELIMIT_DESIGN.md`）
- [ ] **D-27 / D-29** 探针断言 + 不变式用例 + ASan 并发回归进 CI
- [ ] **D-30** 线程入参改 `malloc`/静态 + `tid[2]`（**必须先于 D-20**，否则关闭一返回就野指针）
- [ ] **D-20** `TimerDestory` / `FdTableDestory` / `RouterHashDestory` + 信号退出路径（**D-30 之后**）
- [ ] **D-28** 复测 `test_response` / `test_hash` 剩余泄漏
- [ ] **D-26** 收尾：clean build `-Wall -Wextra` 零告警
- [ ] **D-25** 删 `e0`（随 D-17c 做）

### 6.4 残余告警实测清单（D-26 引用的数据）

**口径 A：真实构建（`make clean && make -j4`，`CFLAGS = -g -O0 -Wall`）**
实测 `build_rc=0`，**17 条告警**：

| # | 位置 | 告警 | 归属 |
|---|---|---|---|
| 1 | `GlobalResource/GlobalResource.c:15` | `control reaches end of non-void function` | BUG-06 家族（`GlobalResourceInit` 缺 `return`） |
| 2 | `MemoryPool/MemoryPool.c:42` | 同上 | **D-12**（`MemoryStackExpend` 缺 `return`） |
| 3 | `MyLock/my_rwlock_t.c:23` | 同上 | BUG-06 家族 |
| 4 | `MyLock/my_rwlock_t.c:36` | 同上 | BUG-06 家族 |
| 5 | `MyLock/my_rwlock_t.c:51` | 同上 | BUG-06 家族 |
| 6 | `StaterMain/StarterMain.c:19` | 同上 | BUG-06 家族 |
| 7 | `Threads/ReceiverThread.c:26` | 同上 | BUG-06 家族（该返回值在 D-07 里要被用起来） |
| 8 | `Threads/WorkerThread.c:149` | 同上 | BUG-06 家族 |
| 9 | `FdConnectResource/SendPointerPool/SendPointerPool.c:63` | 同上 | BUG-06 家族 |
| 10 | `FdConnectOrClose/FdConnectOrClose.c:46` | `unused variable 'n'` | 冗余（`int n=write(...)` 结果未校验） |
| 11 | `RequestAndResponse/Response.c:8` | `unused variable 'n'` | 冗余（`ResponseBodySet` 分配结果未检查） |
| 12 | `Threads/ReceiverThread.c:25` | `unused variable 'e0'` | **D-07**（`TasKQueuePush`） |
| 13 | `Threads/ReceiverThread.c:40` | `unused variable 'e2'` | 冗余（`ResponseSendMain`） |
| 14 | `Threads/ReceiverThread.c:54` | `unused variable 'e0'` | **D-07 家族**（`FdConnect` 返回值未检查） |
| 15 | `Threads/ReceiverThread.c:59` | `unused variable 'e1'` | **D-08a 家族**（`RequestComeMain` 返回值未检查） |
| 16 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:19` | `unused variable 'e0'` | **D-25**（BUG-08） |
| 17 | `FdConnectManage/Timer/Timer.c:87` | `variable 'fd' set but not used` | **D-16 (T2)** ★ 真缺陷的铁证 |

> 结论：17 条里 **6 条直接对应本清单的编号**（D-07×3、D-08a、D-12、D-16、D-25），
> 其余 9 条 `-Wreturn-type` 是"返回值即将被使用"的隐患（D-07 修完就得处理）。

**口径 B：加严（`-Wall -Wextra`，逐文件 `-fsyntax-only` 采集）**
实测 **40 条**，分组：

| 告警类型 | 条数 | 主要来源 | 处置建议 |
|---|---|---|---|
| `-Wignored-qualifiers` | 29 | `RequestAndResponse/Request.h:15`：`const char* const RequestGetter(...)`，被每个 TU 重复包含 | **一处改动全清**：去掉返回类型上的顶层 `const`（返回值加 `const` 无意义） |
| `-Wunused-variable` | 7 | `ReceiverThread.c`×4、`FdConnectOrClose.c:46`、`Response.c:8`、`RequestAnalysiser.c:19` | 随 D-07 / D-25 一起清 |
| `-Wunused-parameter` | 3 | 若干未使用的形参 | 加 `(void)x;` 或删形参 |
| `-Wunused-but-set-variable` | 1 | `Timer.c:87` | **D-16 T2** |

> ⚠️ 两个口径**不能相加**：`-fsyntax-only` 不产出 `-Wreturn-type`（口径 A 的 9 条），
> 所以 A=17 与 B=40 是两套互不包含的采集结果。

复现命令：
```bash
cd /home/yaoxianglongde/c_code/c_web
# 口径 A（真实构建）
make clean && make -j4 2>&1 | grep 'warning:'
# 口径 B（加 -Wextra）
for f in $(find . -name '*.c' -not -path './test/*'); do gcc -fsyntax-only -Wall -Wextra -pthread "$f"; done 2>&1 | grep 'warning:'
```

**退出标准建议**：clean build 在 `-Wall -Wextra` 下**零告警**；
优先清 9 条 `-Wreturn-type`（它们对应的返回值马上要被 D-07 使用）。

### 6.5 版本记录

| 日期 | 变更 |
|---|---|
| 2026-10-14 | **D-31 已修复**：任务队列 CAS 改造的 3 处结构性缺陷（计数重复 / 非原子与原子混用 / 计数与环形指针跨同步域）已定位、修复并双向验证 —— 探针 `test_taskqueue_race`（新增，23 PASS）＋ TSAN 零报告 ＋ `website_test.sh` 由 **6 PASS/21 FAIL/段错误** 恢复为 **27 PASS/0 FAIL**。另新增 **D-32**（取消阻塞后 Worker 忙轮询 ⇒ 空闲 CPU 197~200%，附 3 种等待策略）。`test_queues.c` 4 条旧阻塞语义断言同步改写。**新基线：`make -C test run` 286 PASS / 0 FAIL**（原 256）。 |
| 2026-10-14 | **建档**。合并 `STRESS_TEST_REPORT.md`、`TEST_REPORT.md`、泄漏专核与边界专核；**新增 4 条必崩路径（D-04 / D-08a-c / D-17c）与 1 条堆越界写（D-16 T3）**，本轮又新增 **D-30**（线程入参指向 `StartThread` 栈帧 → 优雅关闭的隐藏地雷）；**决定扩容机制作废（D-18）**并给出 5 个扩容点的逐点处置；补 §6 复跑命令 / 证据索引 / 打勾表 / 告警清单；发现 `/tmp` 已清空，故设计文档重新落到 `test/RATELIMIT_DESIGN.md`。复测基线：clean build rc=0（17 告警）、`make -C test run` 256 PASS / 0 FAIL。 |

---
