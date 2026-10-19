# 连接资源泄漏 与 c=300 可用性 —— 复测报告

> 对象：当前仓库工作树（新位图池 + P0-1/P0-2 + 唤醒合并 + 读一次 + `pending` 续投）
> 方法：真实进程压测（同一份 `./main`，`127.0.0.1:5343`），全程读 `/proc/<pid>/status` 与 `/proc/<pid>/fd`
> 结论：**`store_area`（每连接 2KB 暂存块）没有泄漏；c=300 连续三轮无 503、无卡死、fds 恒定。**

---

## 1. 被验证的那条"隐藏 bug"假设

之前的推断是：

> `FdEntryDelete()`（`FdConnectManage/FdConnectTable/FdConnectTable.c:152`）只 `free(m)` 掉 `Fd_Entry` 节点，
> 没有归还该连接在 `store_area` 里占的 2KB 暂存块 ⇒ 每连接漏 2KB ⇒ 1024 连接后池耗尽 ⇒ 全站 503。

## 2. 结论：假设不成立，回收路径本来就是完整的

`FdEntryDelete` 确实不碰 `request_temp_pool`，但**它本来就不该碰**——暂存块的归还点在连接关闭路径上，
而不是 fd 表节点删除处：

| 位置 | 代码 | 作用 |
|---|---|---|
| `FdConnectOrClose/FdConnectOrClose.c:56` | `RequestTempPoolDestory(fd_ob->request_temp_pool, w->store_area);` | **连接关闭**时销毁暂存池对象：先 `RequestTempPoolFree`（把 2KB 块还给 `store_area`），再 `free(h->state_machine)`，最后 `free(h)` |
| `FdConnectResource/RequestTempPool/RequestTempPool.c:30-45` | `RequestTempPoolDestory()` | `begin != NULL` 时调用 `RequestTempPoolFree` |
| `FdConnectResource/RequestTempPool/RequestTempPool.c:66-82` | `RequestTempPoolFree()` | `from_malloc ? free : MemoryPoolFree(pool, begin)`，并把 `begin/end/ptr_b/ptr_e` 全部置空 |
| `RequestCome/RequestComeMain/RequestComeMain.c:29` | `RequestTempPoolAlloc(fd_ob->request_temp_pool, Receiver_Thread->store_area, 2)` | 每条新连接分配 2KB（`store_area`） |
| `RequestCome/RequestComeMain/RequestComeMain.c:123` | `RequestTempPoolFree(...)` | 请求被完整取走、缓冲清空后立即归还 |

`Fd_Entry` 只是 fd → 池对象的**索引节点**；池对象的生命周期绑在连接上（`FdClose` → `FdConnectOrClose`），
所以"删节点却不还暂存块"这个组合在当前代码里根本不会出现。**不需要改 `FdEntryDelete`。**

### 2.1 实测证据（不是靠读代码下的结论）

用串行 churn 客户端（`/tmp/churn.c`：connect → 发 `GET /hello` → 读一次响应 → 立刻 close，
即每条连接都走"响应可能还没发完对端就关了"的路径），对**同一个进程**连续跑 4 组（共 20,000 条，
加上此前一轮 3,000 条，累计 **23,000 条**）：

| 阶段 | 累计连接数 | VmRSS | `fd` 数 | 结果 |
|---|---|---|---|---|
| 起测前（此前已服务 churn 3,000 + wrk 3×20s） | 3,000 | 476 kB | 6 | — |
| churn 5000 | 8,000 | 1,152 kB | 6 | `ok=5000 bad=0 fail=0` |
| churn 5000（第 1 轮） | 13,000 | 1,152 kB | 6 | 每轮 `ok=5000` |
| churn 5000（第 2 轮） | 18,000 | **1,152 kB** | 6 | 每轮 `ok=5000` |
| churn 5000（第 3 轮） | 23,000 | **1,152 kB** | 6 | 每轮 `ok=5000` |

判读：

- 若真按 2KB/连接泄漏，5,000 条就是 **~10MB**，RSS 必须显著上涨；实际 **1,152 kB 一条直线**；
- `fd` 数始终 6（stdin/out/err + epoll + listen + eventfd），**没有 fd 堆积**；
- 每条连接结束时 `RequestTempPoolDestory` 的返回值路径都走到了（否则 `store_area` 早被抽干，第 1000 条左右就该 503）。

> 说明：churn 的 `ok` 判定只看首行 `HTTP/1.1 200`；`fail` 表示 connect/read 失败；`bad` 表示非 200。

## 3. c=300 连续压测（wrk，3 轮）

`wrk -t8 -c300 -d10s --latency http://127.0.0.1:5343/hello`

| 轮次 | 完成请求 | QPS | p50 | p99 | 非 2xx | VmRSS | fds | 复查 `/hello` |
|---|---|---|---|---|---|---|---|---|
| 1（冷） | 39,691 | 3,931 | 49.97 ms | 511.81 ms | 0 | 2,384 kB | 6 | 200 |
| 2 | 86,347 | 8,548 | 27.38 ms | 100.90 ms | 0 | 3,624 kB | 6 | 200 |
| 3 | 93,983 | 9,317 | 29.24 ms | 73.89 ms | 0 | 3,624 kB | 6 | 200 |

判读：

- 三轮都**正常出量**，没有出现"进程活着但 0 请求 / 全部超时"的现象；
- `Non-2xx or 3xx` 行一次都没打印 ⇒ **0 个 5xx**；
- 每轮结束 `fds=6`、`/hello` 仍 200 ⇒ 没有连接/资源堆积；
- RSS 第 2 轮后**停在 3,624 kB**（池页 first-touch 到位后不再增长）。

> 第 1 轮 QPS 低是冷启动（页首次触碰 + CPU 频率爬升），不是服务异常；第 2/3 轮才是稳态。
> 该数值（~9k QPS @ c=300）与前面报告里用自建 bench 测的 56k+ 不可直接横比：wrk 8 线程 + 300 长连接、
> 每请求独立 `write` 的形态更吃"单接待线程"这一瓶颈，且本机 `nproc=2`。

## 4. 流水线精确性回归（5 轮 × 3 形态）

`python3 /tmp/pipe_stress.py`（每连接一次性发 d 个请求，数响应个数）

| 形态 | 期望 | 实收（5 轮均一致） |
|---|---|---|
| 8 连接 × 20 请求 | 160 | **160**（每连接 [20,20,...]） |
| 16 连接 × 20 请求 | 320 | **320**（每连接 [20,...,20]） |
| 4 连接 × 50 请求 | 200 | **200**（每连接 [50,50,50,50]） |

`总失败轮数=0` —— `TempPack` 1024 环 + `pending` 续投在 300 并发长连接之后仍然精确。

## 5. 为什么当初会误判成"暂存块泄漏"

两条现象叠在一起，看起来很像泄漏：

1. 旧版（`t22`/`t23` 副本）在 **c=300** 时确实出现过"进程还在、连接不通、fds 上涨"；而旧池
   `MemoryPoolInit(&store_area,10,6,500,400)` 只给 1KB~32KB 六类预分配，**64KB 以上四类 0 条**，
   一旦暂存区要扩容就直接 `notfull=0 → 503`，503 的连接又会在 `FdClose` 里走一遍错误路径——
   看起来就像"资源被吃光了"。
2. 旧池是"整池归零"语义，任何一次 `MemoryPoolFree` 都要整池 memset，c=300 时这本身就是几十 MB/秒
   的写带宽黑洞，表现同样是"进程活着但几乎不干活"。

换成位图扫描池（按块回收、不整池归零）+ `ExpendTo` 精确扩容 + `pending` 续投之后，这两个现象都消失了，
而"暂存块没还"这条路径从未真正存在——所以本轮**不需要为此改一行代码**，只需把结论记录下来。

## 6. 仍未完成（与本文无关，别混淆）

- `memory_queue` → 无锁 SPSC（`TODO_ISSUES_AND_DEADLOCK_PLAN.md` 第 8 节）；
- `Thing_queue` 投递改非阻塞 + worker 本地待投递链（第 7 节）；
- `sem_timedwait` 保险丝；
- `sendfile` 冷文件跑在接待线程（可选，第 5.22 节）。

## 7. 复现命令

```bash
# 1) 起服务（当前工作树，端口 5343）
cd /home/yaoxianglongde/c_code/c_web && make && ./main &

# 2) 连接 churn（观察 RSS/fd 是否随连接数上涨）
gcc -O2 -o /tmp/churn /tmp/churn.c
P=$(ss -lntp | sed -n 's/.*127.0.0.1:5343.*pid=\([0-9]*\).*/\1/p')
for i in 1 2 3; do /tmp/churn 5343 5000; grep VmRSS /proc/$P/status; ls /proc/$P/fd | wc -l; done

# 3) c=300
wrk -t8 -c300 -d10s --latency http://127.0.0.1:5343/hello

# 4) 流水线
python3 /tmp/pipe_stress.py
```
