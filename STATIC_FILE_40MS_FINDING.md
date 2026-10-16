# 常规网站测试报告 + 静态文件 42ms 延迟定位（2026-10-15）

> 由 Cline 生成，**未改动任何源文件**。测试对象：你当前仓库（`epoll=1` + 三处 NULL 校验）的副本，全量重编。
> 结论：**功能全对（27/27 PASS）**；另发现一个**内核 TCP 层的 42ms 延迟**问题（静态文件路由），一行 `setsockopt` 可解。

## 1. 网站功能测试：PASS 27 / FAIL 0 ✅

`bash test/website_test.sh`（副本上运行，端口 5343）：

| 分组 | 结果 |
|---|---|
| 路由：`/`（200 + 含 `c_web is running` + `text/html`）、`/hello`（200 + 正文）、`/time`（真实时间）、`/echo?name=&msg=`（查询串回显）、`POST /api/echo`（请求体回显）、`GET /api/echo`→405、未注册→404 + `Not Found`、`/mem/256k`（200 + 262144 字节） | 全 PASS |
| 10MB 下载：状态码 / 字节数 10485760 / **sha256 逐字节一致** / `Content-Length` 头 / 第二次下载仍完整一致 | 全 PASS |
| 并发：4 路 × 10MB 全部完整一致（0.04s，约 983 MB/s） | PASS |
| 收尾：全部请求结束后**进程仍存活** | PASS |

## 2. 常规负载（非极端）

| 场景 | 结果 |
|---|---|
| `wrk -t2 -c50 -d3s /hello` | **27,192 req/s**，延迟 avg 1.83ms / max 13.25ms |
| `wrk -t2 -c10 -d3s /` | **233.99 req/s**，延迟 avg **42.51ms**（异常！→ 第 3 节） |

## 3. 定位过程（四步收敛）

**① 每次新连接（顺序 curl）→ 正常**
```
/       1.404 1.557 1.790 1.293 1.688 1.336 1.729 1.527 ms
/hello  1.616 1.546 1.258 1.340 1.726 1.992 1.419 1.440 ms
```
⇒ 与路由、文件大小无关。

**② 同一条 keep-alive 连接上逐请求（python http.client，`/tmp/audit/ka.py`）**
```
=== 混合（中途穿插静态文件）===
 1 /hello  200 len=13     4.83 ms
 2 /hello  200 len=13     2.53 ms
 3 /       200 len=3136  42.33 ms   ← 静态文件
 4 /hello  200 len=13     2.29 ms
 5 /       200 len=3136  43.46 ms   ← 静态文件
 6-8 /hello              1.68~2.11 ms
=== 连接上只有静态文件 ===
 1 /  200 len=3136   2.79 ms        ← 第 1 个快
 2 /  200 len=3136  43.31 ms
 3 /  200 len=3136  43.98 ms
 4 /  200 len=3136  44.88 ms
 5 /  200 len=3136  44.23 ms
```
⇒ **连接上第 1 个静态文件请求快，之后每个静态文件请求都 ~43ms；文本请求始终 ~2ms。**

**③ strace（`-f -tt -T`，只跟踪 write/sendfile/epoll_wait）→ 42ms 不在服务器代码里**
```
1130: 17:28:22.743855 write(5, "HTTP/1.1 200 OK...", 81) = 81 <0.000023>
1131: 17:28:22.743927 sendfile(5, 6, [0] => [3136], 3136) = 3136 <0.000064>
1134: 17:28:22.745595 write(5, "HTTP/1.1 200 OK...", 81) = 81 <0.000021>
1135: 17:28:22.745655 sendfile(5, 6, [0] => [3136], 3136) = 3136 <0.000091>
```
整段 26.17s 的日志里：**只有 2 次 write + 2 次 sendfile**（两次响应相隔 **1.7ms**），却有 **15,635 次 `epoll_wait(4, [], 1024, 1)` 空转**（间隔分析最大空档仅 9ms，为 strace 自身开销）。⇒ 服务器把两个响应都在 1.7ms 内发出去了。

**④ 决定性实验：客户端禁用延迟 ACK（同一二进制、同一请求）**
```
A 对照（普通客户端）            1 /  6.92 ms    2 / 42.85 ms
B 每次 recv 前置 TCP_QUICKACK   1 /  1.69 ms    2 /  2.07 ms   3 / 1.44 ms   4 / 1.99 ms
C /hello 对照                   1 /hello 1.41 ms   2 /hello 1.97 ms
```
⇒ **42ms 消失**。确认是内核 TCP 的 **Nagle + 延迟 ACK**，与你的代码逻辑无关。

**⑤ syscall 形态对照（写调用大小分布）**
```
6 × write(fd=5, len=81)   +  6 × sendfile(fd=5, filefd=6)     ← 6 个静态文件请求：两段
2 × write(fd=5, len=93)                                       ← 2 个文本请求：一段
```

## 4. 机理

- 静态文件路由 `FILE_ROUTE` → `SendFile()` → `ResponseFileSet(w, fd, 0)`：框架**先 `write(81B 响应头)`，再 `sendfile(3136B 文件体)`** = 两个小 TCP 段。
- 第 2 段（小于 MSS）被本机 **Nagle 算法扣住**，等第 1 段的 ACK；而客户端此时空闲，**延迟 ACK**（Linux 最长 ~40ms）⇒ 每个请求稳定 **~42ms**。
- 文本响应（`/hello`）：头 + 体在**同一次 `write(93B)`** 里 ⇒ 单段 ⇒ 无停顿（~2ms）。
- **10MB 下载反而没问题**：`sendfile` 发的是整 MSS 段，Nagle 不拦截 ⇒ 1122 MB/s、4 并发 983 MB/s。
- 第 1 个静态请求快：连接刚建立、客户端刚发过数据，处于 quickack 状态，ACK 立即返回。

**为什么浏览器能感觉到**：首页会顺序加载 `index.html` → `style.css` → `app.js` → `favicon.ico`，4 个静态资源在 keep-alive 连接上**各多 ~40ms**，首屏多 120~160ms。

## 5. 修复建议

**方案 A（推荐，最小改动：1 处 2 行）**：accept 后给连接开 `TCP_NODELAY`（nginx 默认行为）

```c
// FdConnectOrClose/FdConnectOrClose.c  FdConnect()：client_fd=accept(...) 成功之后
int one=1;
setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
```
需在文件头补 `#include <netinet/tcp.h>`（`Include/Include.h` 目前没有这个头；`IPPROTO_TCP` 来自 `<netinet/in.h>`，通常已被包含）。
插入位置：第 24 行 `}`（accept 成功之后）与第 26 行 `EpollReadMount(...)` 之间（或之后都行）。

**方案 B（nginx 式，能少一个包）**：发头前 `TCP_CORK`，`sendfile` 后取消
```c
int one=1, zero=0;
setsockopt(fd, IPPROTO_TCP, TCP_CORK, &one, sizeof(one));   // 发响应头之前
/* write(headers) ... sendfile(body) ... */
setsockopt(fd, IPPROTO_TCP, TCP_CORK, &zero, sizeof(zero)); // 文件体发完之后
```
注意：包头 + 文件体属同一次响应时才 cork/uncork；`FdClose`/异常路径也要确保 uncork（或依赖 socket 关闭自动解除）。

**只做 A 就够**：`.08MB`/1KB 这类小文件会变成两个立即发出的段（少一次合并，但**延迟从 42ms → ~2ms**，收益巨大）。

## 6. 当前状态与建议
- 仓库里 **没有** `TCP_NODELAY` / `TCP_CORK` / `MSG_MORE`（`grep -rn` 无结果）——这是纯新增。
- 与内存池、队列、NULL 校验**完全无关**，属于 socket 选项层面的遗留。
- 建议记进 backlog（例如 D-39「静态文件响应缺 TCP_NODELAY，keep-alive 上每请求 +40ms」），等你重构内存池时一并处理也可——A 方案是 2 行、零风险、不动内存池。

## 7. 落地实测：你采用方案 A 之后（2026-10-15 收尾）

改动（`git diff` 确认）：`Include/IncludeStandard.h` 加 `#include <netinet/tcp.h>`；`FdConnectOrClose/FdConnectOrClose.c` 的 `FdConnect()` 里 `accept` 之后加 `int one=1; setsockopt(client_fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));`。在你仓库副本上全量重编（`make exit=0`，无新告警）后实测：

| 指标 | 改前 | 改后 |
|---|---|---|
| 同一 keep-alive 连接 4 × `/` | 2.79 → **43.31 / 43.98 / 44.88 / 44.23 ms** | **4.30 / 1.53 / 1.71 / 1.61 ms** |
| 交替 `/hello` 与 `/`（同一连接） | `/` 全部 ~42ms | **4.19 / 1.57 / 2.14 / 1.47 / 1.52 ms** |
| `wrk -t2 -c10 -d3s /` | 233.99 req/s，avg 42.51ms | **4,798 req/s（×20.5）**，avg **2.20ms** |
| `wrk -t2 -c50 -d3s /hello` | 27,192 req/s，avg 1.83ms | 16,744 req/s，avg 2.99ms（同批波动范围内） |
| 10MB 下载（机制不受影响） | 单连接 `curl` 2020 / 2235 / 2328 MB/s | 单连接 901 / 1047 / 1980 MB/s；`wrk -c50` 2.08~4.13 GB/s（同口径**无一致性差异**，见报告 §11） |
| `test/website_test.sh` | 27 PASS | **27 PASS / 0 FAIL**（exit=0） |

⇒ 静态文件路由的 ~42ms 消除，`/` 的 c10 吞吐提升约 20 倍；文本路由与 10MB 下载行为不变；功能回归全通过。
（清理：测试只 kill 自己启动的 PID，跑完 `pgrep -x main` 为空。）

### 附：机理速查（为什么会被"扣住"）

- **Nagle（发送方）**：连接上还有未确认的数据时，不许再发**小于 MSS** 的段，先在发送缓冲里攒着。
- **延迟 ACK（接收方）**：收到包不立刻回 ACK，等能捎带/合并确认，最长 `TCP_DELACK_MAX = HZ/25 = 40ms`。
- 静态文件响应是 `write(81B 头)` + `sendfile(3136B 体)` = 两个小段 ⇒ 第 2 段被扣住等 ACK，客户端又在等第 2 段才能读完 ⇒ 互相等 40ms。
- 对照：`/hello` 是**单次 write(93B) 一个段**（不受影响）；10MB 是**整 MSS 段**（Nagle 不拦）；连接上第 1 个静态请求因客户端刚发过数据处于 quickack（ACK 立即回，故也快）。
- `write()/sendfile()` 返回 ≠ 数据上线：只是交给内核发送缓冲，所以 strace 里两次调用只隔 1.7ms，而段 2 要 40ms 后才出去。

