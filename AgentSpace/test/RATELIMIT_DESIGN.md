# c_web 限流与容量自检设计（RATELIMIT_DESIGN）

> **文档状态**：**重建版**。原稿 `/tmp/boundary/RATELIMIT_DESIGN.md` 已随 `/tmp` 被清空而丢失，
> 本文按同样结论重新落盘进仓库，此后**以本文为准**。
> **关联**：`test/ISSUE_BACKLOG.md` 的 **D-21**（本文就是它的方案）、**D-17**（"并列上限"的活例子）、
> **D-18**（扩容机制作废的决定）、**D-29**（不变式断言）。
> **一句话**：扩容已作废 ⇒ **容量就是唯一的安全机制** ⇒ 容量必须显式、可校验、超限明确拒绝。

---

## 1. 为什么要做（而不是"直接把并发调大"）

1. 系统里至少有 8 个互相独立、**全靠人脑记住**的容量数字（见 §2），它们之间的一致性**没有任何代码检查**；
2. 压测实测：某个尺寸类的块被耗尽后，表现是"一大批 503"，而**日志里什么都没有**，只能靠人肉推断根因；
3. 更糟的是"耗尽"之后的行为**没有统一约定**：有的路径回 503、有的路径拿着空指针继续跑
   （D-04 / D-17c 就是这么崩的）；
4. 由于**扩容被取消**（D-18），"容量不够"不再能靠"长大"来兜底 ⇒ 必须有**显式的容量契约**。

---

## 2. 现有常量（全部来自源码实测，非估算）

| 常量 | 值 | 位置 |
|---|---|---|
| 全局 `http_pool` | 每尺寸类 **400 块**（6 类：1/2/4/8/16/32 KB） | `Threads/ReceiverThread.c:8` `MemoryPoolInit(&http_pool,6,6,500,400)` |
| send 池 `store_area` | 每尺寸类 **400 块**（10 类 1 KB~512 KB，实际只初始化前 6 类） | `Threads/ReceiverThread.c:9` `MemoryPoolInit(&store_area,10,6,500,400)` |
| `Thing_queue`（收→发） | 100 | `Threads/ReceiverThread.c:10` |
| 定时器容量 | 5000 | `Threads/ReceiverThread.c:11` `TimerInit(&my_Timer,5000)` |
| 连接表桶数 | **59**（`FdTableInit(&fd_table,4)` ⇒ `PRIME_BUCKET_SIZES_3[3]`；**该表没有扩容函数**） | `Threads/ReceiverThread.c:12` + `FdConnectManage/FdConnectTable/FdConnectTable.c:4-9/27` |
| 每 Worker `memory_queue` | 100 | `Threads/WorkerThread.c:76` |
| 每 Worker `txt_pool` | 每尺寸类 **120 块**（7 类 1~64 KB） | `Threads/WorkerThread.c:77` `MemoryPoolInit(&txt_pool,7,7,200,120)` |
| 每 Worker `http_pool` | 每尺寸类 **10 块**（6 类 1~32 KB） | `Threads/WorkerThread.c:78` `MemoryPoolInit(&http_pool,6,6,10,10)` |
| `listen` backlog | 100 | `StaterMain/StarterMain.c:53` `listen(*Fd, 100)` |
| Worker 线程数 `N_w` | **2** | `StaterMain/StarterMain.c:64` `WorkerThreadInit(&p1,1)` / `:70` `WorkerThreadInit(&p2,2)` |
| 连接空闲超时 | **60 s** | `StaterMain/StarterMain.c:78` `.time = 60`（`TimerOvertime` 据此判超时） |
| `taskQueue` 容量 | **100**（由 `WebStater` 传入） | `GlobalResource/GlobalResource.c:7`/`:13`；实参见 `StaterMain/StarterMain.c:93` `GlobalResourceInit(100,Handler)` |
| `ed_store_blocknum` | **100**（`main_t.ed_store_blocknum=100` → `ReveiverThreadMain` → `RequestComeMain`） | `StaterMain/StarterMain.c:78`；`RequestCome/RequestComeMain/RequestComeMain.c:11` |

---

## 3. 不变量（就是要交给机器校验的那几条）

**符号**（全部为"每尺寸类"意义上的块数）：

| 符号 | 含义 | 实测值 |
|---|---|---|
| `P_http` | 全局 `http_pool` 每类块数 | **400** |
| `P_store` | send 池每类块数 | **400** |
| `P_txt` | 每 Worker `txt_pool` 每类块数（打包响应用） | **120** |
| `P_hw` | 每 Worker `http_pool` 每类块数（**请求解析用**） | **10** |
| `N_w` | Worker 线程数 | **2** |
| `C` | 最大并发连接数（= `listen` backlog 与各队列深度的综合上界） | 100 |
| `c` | 单连接在途响应包上限（= `ed_store_blocknum`） | 100 |
| `G_parse` | 全局同时"解析中"的请求数 | 未受控 |
| `W_pack` | 单 Worker 同时"打包中"的响应数 | 未受控 |

| # | 不变量 | 为什么 | 代入现状 | 判定 |
|---|---|---|---|---|
| **I1** | `C ≤ P_http` | 每个连接至少占 1 块请求分析块（63 KB 类） | 100 ≤ 400 | ✅ |
| **I2** | `C ≤ P_store` | 每连接至少占 1 块 send 池块 | 100 ≤ 400 | ✅ |
| **I3** | `C ≤ Timer 容量` | 每连接一个定时器条目 | 100 ≤ 5000 | ✅ |
| **I4** | `G_parse ≤ N_w × P_hw` | **解析对象出自每 Worker 只有 10 块的 `http_pool`** | 并发 100 时可同时有 100 个请求要解析 → 100 > **20** | ❌ **不成立**（D-17a） |
| **I5** | `C × c ≤ P_store` | 全局在途响应包不能超过 send 池块数 | 100 × 100 = **10,000 ≫ 400** | ❌ **不成立**（D-17b，**这就是压测里那道 ~400 的墙**） |
| **I6** | `W_pack ≤ P_txt`（每 Worker） | 打包中的响应各自占 1 块 `txt_pool` 的 64 KB 类 | 无任何代码保证 | ❌ **不成立/未保证**（D-04 的触发点） |
| **I7** | `listen backlog ≤ 下游队列深度` | accept 进来但队列塞不下 ⇒ 请求被丢弃 | 100 vs 100 | ⚠️ 相等，**无余量** |
| **I8** | `c ≥ 1` 且 `c ≤ P_store / C` | `c` 是"每连接"上限，必须有全局约束 | 100 ≫ 400/100 = 4 | ❌ 由 I5 覆盖 |

**结论（必须修正才能启用自检）**：
- ❌ **I4 / I5 / I6 明确不成立**，⚠️ I7 无余量；
- **修法只有两条路**，二选一或组合：
  1. **把池按上界放大**：`P_store ≥ C × c`（例如 C=100、c=4 ⇒ 400 刚好；c 要保持 100 就必须 C=4，显然不可接受）
     ⇒ 现实做法是 **把 `c` 压到 4~8**，或把 `store_area` 的每类块数提到 `C × c`；
  2. **把并发压到与池匹配**：`C ≤ P_hw × N_w` 且 `C × c ≤ P_store`，
     并把每 Worker `http_pool` 的 **10 块**提到与全局同量级（D-17a）。
- 在修好之前**不能启用自检**，否则服务会拒绝启动 —— 这恰恰说明自检是有价值的：
  **它把"压测时才暴露的墙"提前到了启动时刻。**

---

## 4. 模块与改动点（最小侵入）

### 4.1 新增 `Limit/GlobalLimit/`（零 Makefile 改动）

根 Makefile 第 17 行是 `SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)`，
新增的 `Limit/GlobalLimit/*.c` 深度正好 2 层、落在 `*/*/*.c` 上，会被**自动收编**
（已核实 Makefile 与 `test/Makefile` 的说明），**不需要改任何 Makefile**。

```c
// Limit/GlobalLimit/GlobalLimit.h
typedef struct {
    int max_conn;          // C    最大并发连接
    int max_global;        //      全局同时处理数
    int max_per_conn;      // c    = ed_store_blocknum（每连接在途包上限）
    int max_worker_parse;  //      G_parse 上界（每 Worker）
    int worker_num;        // N_w
    int pool_http;         // P_http
    int pool_store;        // P_store
    int pool_txt;          // P_txt（每 Worker）
    int pool_http_worker;  // P_hw （每 Worker）
    int listen_backlog;
} Global_Limit;

int GlobalLimitCheck(const Global_Limit* L);   // 1 = 全部成立；0 = 有不成立（自行打印原因）
```

`GlobalLimitCheck` 里逐条核对 §3 的 I1 ~ I7，**失败时打印"第几条不变量 + 实际值 + 期望关系"**
（例如 `[I5 FAIL] C*c = 100*100 = 10000 > P_store(400)`）。

### 4.2 接入点

- `GlobalResource/GlobalResource.c` 的 `GlobalResourceInit()` —— 该函数当前**没有 `return`**
  （§6.4 口径 A 的第 1 条告警），正好一起补上：校验失败 `return -1`。
- **常量收敛到一处**：把 §2 表里的数字集中到 `GlobalResource` / `Limit`，再由它分发给
  `MemoryPoolInit` / `TimerInit` / `xQueueInit` / `listen`，杜绝"同一个数字在两个文件里各写一遍"。
- `C` 与 `ed_store_blocknum` 必须由**同一组配置推导**，避免再出现"100 与 400 的关系写错"。

### 4.3 明确不做的事（与扩容作废保持一致）

- **不做动态扩容**（D-18 已作废）：容量不够就是**拒绝**，不是长大；
- **不做"自动重试 / 降级扩容"**：申请不到 ⇒ 打日志 + 回 503（现成路径）；
- 运行期限流（超限停止 `accept` / 拒绝新连接）**排在自检之后**，且不改动既有业务逻辑。

---

## 5. 实施顺序（硬性）

1. 先修 **D-01 / D-02 / D-03 / D-04 / D-07 / D-08 / D-17c** ——
   限流会把并发推高，否则"偶发崩溃"直接变"必现崩溃"；
2. 加**只读统计接口**（各池在用/空闲块数、各队列 `num`/`notfull`）—— D-29 与探针都依赖它；
3. 把 §2 的常量**收敛到一处**（§2 已全部查实：`taskQueue`=100、连接表=59 桶、`N_w`=2、连接超时 60 s）；
4. 写 `GlobalLimitCheck` 并接入 `GlobalResourceInit`；
5. 用 §3 的两条路**修正 I4 / I5 / I6**（否则自检必然"拒绝启动"，服务起不来）；
6. 最后才做运行期限流。

---

## 6. 验收

| 用例 | 期望 |
|---|---|
| 故意把 `ed_store_blocknum` 设成 400（违反 I5） | 进程**拒绝启动**，日志指出"第 5 条不变量不成立：400×100 > 400" |
| 故意把 `C` 设成 0 / 负数 | 拒绝启动并指出非法值 |
| 正常配置 | 正常启动；`make -C test run` → **256 PASS / 0 FAIL** 无回归 |
| 正常配置 + `wrk -t2 -c100 -d30s` | 不再出现"某尺寸类突然耗尽"却**没有任何日志**的情况（配置侧由自检覆盖） |
| 修好 I4（把每 Worker `http_pool` 的 10 块提到合理量级） | 并发 100 下"解析对象耗尽型 503"消失 |

---

## 7. 变更记录

| 日期 | 变更 |
|---|---|
| 2026-10-14 | **重建**（原 `/tmp/boundary/RATELIMIT_DESIGN.md` 已随 `/tmp` 清空丢失）；补齐"实测常量表"、I1~I8 代入判定，明确 **I4/I5/I6 不成立、I7 无余量**及两条修法；写明"不做扩容"的边界与实施顺序。 |
