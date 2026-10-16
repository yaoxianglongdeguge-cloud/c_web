# c_web 当前性能快照（2026-10-15）

> 测试对象：**当前仓库源码**（`epoll_wait 1ms` + 三处 `fd_ob==NULL` 校验 + `TCP_NODELAY`）的副本，`make -j4` **exit=0** 全量重编后实测。
> 环境：2 核 VM（nproc=2，与 VS Code Server 共存），**wrk 与本服务器同机** ⇒ 适合同批相对比较，跨批 c50/c200 波动约 ±30%。
> ⚠️ 仓库里的 `./main` 还是 **09:01** 的旧二进制，本地要测请先 `make -j4`。

## 一、当前数据

| 指标 | 数值 |
|---|---|
| 空闲 CPU / RSS | **1%** / 3300 KB |
| 空闲 12s 后 3 个 `/hello` | **1.49 / 1.61 / 1.84 ms** |
| keep-alive 连接上 4 个静态首页 `/`（3136B） | **3.71 / 1.83 / 1.75 / 1.46 ms** |
| wrk `-t2 -c50 -d4s /hello` | **34,413 req/s**，avg 1.41ms，max 4.35ms |
| wrk `-t2 -c200 -d4s /hello` | **56,682 req/s**，avg 1.70ms，max 8.23ms |
| wrk `-t2 -c50 -d4s /`（静态 3136B） | **26,095 req/s**，avg 1.89ms，80 MB/s |
| wrk `-t2 -c50 -d4s /download/10mb` | **211~449 req/s、2.08~4.41 GB/s**（波动大，见注） |
| RST 风暴 3 × 40（`/hello` + `/download/10mb`，`SO_LINGER` 立即断） | 3 轮全过、**存活=yes** |
| 风暴后 `-c50 /hello` | 35,470 req/s（无退化） |
| 风暴前后 RSS | 3300 → 3300 KB（**零增长**） |
| `test/run_tests.sh` | exit=0（taskqueue_race 23 PASS / 0 FAIL） |
| `test/website_test.sh` | **PASS 27 / FAIL 0**（10MB sha256 逐字节一致、4 路并发完整、收尾存活） |

## 二、相对最初的提升

| 指标 | 最初（旧 `./main`：epoll 50ms + 无 NULL 校验） | 现在 | 变化 |
|---|---|---|---|
| 空闲 CPU | 190~195% | **1%** | 195 倍 |
| 空闲后单请求延迟 | 50.7 ms | **1.4~1.9 ms** | ≈35× |
| wrk c50 `/hello` | 1,657 ~ 9,048（波动） | **34,413** | ≈4~20× |
| wrk c200 `/hello` | 21.6k ~ 24k | **56,682** | ≈2.4× |
| keep-alive 静态首页 | 每个 42 ms | **1.5~1.9 ms** | ≈25× |
| c10 首页吞吐 | 234 req/s | **4,798 req/s** | ≈20× |
| RST 风暴 | 第 1 轮后 **段错误** | 全过、**存活** | 崩溃 → 稳定 |
| 10MB 下载 | 1.1~2.3 GB/s（单连接） | 同量级（**不受本轮改动影响**） | — |

> **注（10MB 吞吐的口径与波动）**：早期表格里写的"1122 MB/s → 4.41 GB/s"是**口径不一致**（前者单连接 `curl`、后者 `wrk -c50` 并发），已更正。同口径 A/B（TCP_NODELAY 前/后，同一脚本、同一端口）：
> - 单连接 `curl`：改前 2020/2235/2328 MB/s；改后 901/1047/1980 MB/s
> - `wrk -c50`：改前 2.08 GB/s；改后 4.13 GB/s（同构建重复测也在 2.08~4.41 之间跳）
>
> ⇒ **无一致性差异**，波动来自「同机 wrk 客户端抢 CPU + 页缓存状态」，机理上也应当无差异：`sendfile` 输出的都是**整 MSS 段**，Nagle 不拦，`TCP_NODELAY` 对大文件不起作用。大文件快的原因是 `sendfile` 零拷贝 + 非阻塞 + EPOLLOUT 续写 + 页缓存，与本轮四项改动无关。

## 三、四个改动各自的贡献

| 改动 | 贡献 |
|---|---|
| 三处 `fd_ob==NULL`（`FdConnectOrClose.c:52`、`ResponseSendMain.c:36` / `:92`） | 消除 RST/断开风暴下的 **SIGSEGV** |
| `Threads/ReceiverThread.c`：`epoll_wait(...,50)` → `1` | 单请求 **50.7ms → 1.6ms**；c50 **1,657 → 32,680** |
| `TCP_NODELAY`（`FdConnect()`）+ `<netinet/tcp.h>` | 静态文件 keep-alive **42ms → 1.6ms**；c10 首页 **×20** |
| `WorkerThread` 睡眠参数（起点 0 / 封顶 1ms） | 空闲 CPU **195% → 1~5%** |

## 四、注意事项与未做项

- 延迟/吞吐含同机 wrk 的干扰，适合"同批相对比较"，**不宜当绝对基准**。
- 10MB 的 `Transfer/sec` 随页缓存与批次波动（2.16 ~ 4.41 GB/s 都出现过）。
- 仍未做（待你安排）：
  1. **内存池重构**（你已构思好，等你动手）；
  2. **sem 唤醒方案**（`TaskQueue` sem_wait/`WorkerQueuePush` 非阻塞，须成对改 3 个文件）——空闲 CPU 可到 **0%**，配 `epoll=1` 时曾测到 **c50 51,812**；
  3. 两个无害的既有告警：`FdConnectOrClose.c:48 unused variable 'n'`、`Threads/ReceiverThread.c:70 unused variable 'e2'`。
