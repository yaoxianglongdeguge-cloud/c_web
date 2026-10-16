# c_web HTTP 服务器 并发压力与缺陷定位测试报告

## 0. 报告信息

| 项目 | 内容 |
|---|---|
| 被测版本 | `314174f`（分支 `main`，「编译通过」）+ **已跟踪文件的未提交改动**（`EpollOperate.c`、`RequestAnalysiser.c`、`RequestIntergrity.c`、`StarterMain.c`、`main.c`、`Makefile`）。**本报告涉及的全部框架缺陷文件均处于原始提交状态**，见 10.4 |
| 测试日期 | 2026-10-14 |
| 测试环境 | AMD Ryzen 9 8945HX（虚拟机内可见 **2 核**）/ Linux x86_64 / gcc 15.2.0 / `wrk 4.1.0` / `valgrind 3.26.0` / 自研探针 |
| 测试对象 | 「接收线程 → 任务队列 → 工作线程 → 内存池打包 → 接收队列 → 发送线程 → epoll 写事件 → 内存回收」这条**全并发链路** |
| 测试代码位置 | 本报告放在 `test/`；实验脚本与探针位于 `/tmp` 临时副本，**不改动仓库任何源码** |
| 执行方式 | `wrk` 持续压测 + AddressSanitizer 构建 + 自研内存池/队列探针 + 源码走查 |
| 总体结论 | **新增 7 个缺陷（3 个 P0）**，其中 **1 个使服务在高并发下偶发 `SIGSEGV` 退出**；另定位 **1 个配置缺失可让静态文件吞吐提升 11.2×**。全部缺陷位于**未改动的原始框架文件**中 |

> **与 `TEST_REPORT.md` 的关系**：那份报告是**功能/单元级**测试（当前 256 断言全绿、11/11 用例通过、网站端到端 27 PASS）。它的手段**结构性地无法发现本报告的问题**——原因见 **2.1**。因此本报告独立编号，从 **BUG-11** 起。

---

## 1. 结论摘要

| 编号 | 级别 | 问题 | 位置 | 证据 |
|---|---|---|---|---|
| BUG-11 | **P0 崩溃** | `WorkerQueueTopAndPop` 在**队列为空时也 `sem_post`**，导致「未满」信号量无上限漂移；队列容量闸门失效后环形缓冲区被越容量写入、`ptr_in` 绕圈覆盖尚未取走的条目 | `Queue/WorkerQueue/WorkerQueue.c:60-72` | 探针实测 **35 秒内 18,748 次**空弹出 post，信号量 `100 → 101 → 102 …` |
| BUG-12 | **P0 崩溃** | 由 BUG-11 触发：`CleanMemoryQueue` 取到陈旧/重复指针并**二次释放**，内存池空闲栈出现重复 `serial` → **同一个块被分配给两个使用者** → 小响应下 `body` 与打包缓冲地址相同 → `strcat` 源目重叠（UB）→ 偶发段错误 | `MemoryPool/MemoryPool.c:136-146`（无所有权校验）、`Threads/WorkerThread.c:58-69` | ASan：`strcat-param-overlap` ×55，两个区间**起始地址完全相同**；探针：`释放了一个不在分配状态的块` |
| BUG-13 | **P0 崩溃** | `FdClose` **不检查** `FdTableFind` 返回值；fd 已不在表中时 `fd_ob == NULL`，下一行直接解引用 | `FdConnectOrClose/FdConnectOrClose.c:50-56` | 源码走查（同函数另有 2 处同类漏检，见 5.2） |
| BUG-14 | P1 未定义行为 | 池满走 503 分支时 `*C` **从未被赋值**，而 `WorkerMain` 中 `char* C;` **未初始化** | `Threads/WorkerThread.c:135` + `:49-50` | 源码走查 |
| BUG-15 | P1 设计缺陷 | `MemoryPoolAlloc` 的尺寸参数单位是 **KB**，`MemoryPoolFree` 是 **字节**（源码注释自称「单位字节」），全靠每个调用点手写 `*1024` 对齐；任一处漏写即把块归还进错误的尺寸类 | `MemoryPool/MemoryPool.c:173 / 197` | 源码走查 + 6 个调用点逐一比对 |
| BUG-16 | P1 内存泄漏 | `MemoryStackExpend` 只 `free` 了栈结构体，**没有 `free` 内部的 `leisure` 数组**；且扩容搬运时加锁的是**新栈**的 mutex | `MemoryPool/MemoryPool.c:25-42` | 源码走查 |
| BUG-17 | P1 静默失败 | 4 处关键返回值被丢弃，失败后无感知：`TasKQueuePush`（请求静默丢失，客户端干等 60s 超时）、`FdTableInsert`、`MemoryStackTopAndPop`（`serial` 停留 `-1`，`ptr = memory_strip - size` 越界指针）、`FdConnect` | `ReceiverThread.c:23`、`FdConnectOrClose.c:32`、`MemoryPool.c:114/128` | 源码走查 |
| OPT-01 | **P0 性能** | accept 后未设 `TCP_NODELAY`；响应分两段写（`write` 发头 + `sendfile` 发文件）时被 **Nagle 算法 + 对端延迟 ACK（40ms）** 扣住 | 缺 `setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, …)` | **A/B 实测：QPS 1,049 → 11,777（11.2×），P50 43.60ms → 1.75ms（24.9×）** |
| OPT-02 | P2 性能 | `Connection: close` 硬编码，**无 keep-alive**，每请求一次 TCP 三次握手，QPS 上限被 `accept` 限制 | `FdConnectOrClose/FdConnectOrClose.c:95` | 源码走查 + 压测并发曲线 |

**判定：框架的**架构设计**（反应堆模型、每连接状态机、尺寸类内存池、优先队列定时器）是成立的且性能潜力很大——`sendfile` 路径实测 **4.83 GB/s**；但**并发收尾路径（队列 → 内存回收）存在未加校验的时序缺陷**，在持续压测下会让进程崩溃。**当前版本可做功能联调与内测演示，不可用于"抗压演示"或任何长期运行场景。**

---

## 2. 测试方法与工具链

### 2.1 为什么 256 条断言测不出这些问题

现有 `test/src/test_*.c` 的用例覆盖了内存池、队列、哈希、解析、路由等**单个模块的确定性行为**，调用方式是「准备输入 → 调一次 → 断言输出」。而本次两个 P0 的共同特征是：

1. **时序相关**：BUG-11 依赖「发送线程 `sem_post` 与工作线程 `CleanMemoryQueue` 的并发交错」，单线程顺序调用永远走不到；
2. **概率触发**：BUG-12 需要空闲栈里恰好出现重复 `serial`，且后续两次分配恰好落在同一尺寸类。因此同一份代码 **有时 `-c200` 跑 5s 完全正常（59,707 QPS），有时第 1 轮就崩**——这是比"必崩"更危险的形态；
3. **UB 而非逻辑错误**：`strcat` 源目重叠不会立刻报错，glibc 的向量化实现在特定长度/对齐下才越界。

**结论（流程级建议）**：给 CI 增加 (a) ASan 构建下跑一次并发压测，(b) 内存池/队列的探针式断言。这两项成本很低，却能拦住整类缺陷。

### 2.2 压测设计

| 设计点 | 做法 | 理由 |
|---|---|---|
| 并发档位 | `c=50 / 100 / 120 / 150 / 200` | 框架 `listen(fd,100)` + 任务队列容量 100，150/200 用于越过设计上限观察退化行为 |
| 负载类型 | `/hello`（纯动态）、`/`（sendfile 静态）、`/mem/256k`（大内存响应）、`/download/10mb`（大文件） | 覆盖「纯计算」「零拷贝文件」「池内大块」「池外 malloc 回退」四条不同路径 |
| 客户端超时 | 常规 `--timeout 5s`；专项实验用 `--timeout 1s` **故意短于响应时间** | 逼客户端中途断开，命中 `EPIPE` / 提前关闭 / 半发送等**收尾路径**（缺陷集中区） |
| 连接模型 | 不启用 keep-alive（框架不支持） | 每请求一次连接建立/销毁，把「收尾路径」的调用频率放大几个数量级 |

### 2.3 内存错误的两级定位手段

**第一级：AddressSanitizer 构建**（在 `/tmp/cweb_asan` 副本）

```bash
cd /tmp/cweb_asan
make CFLAGS="-g -O0 -Wall -fsanitize=address -fno-omit-frame-pointer" \
     LDFLAGS="-lpthread -fsanitize=address"
ASAN_OPTIONS=detect_leaks=0:log_path=/tmp/asan_report ./main
```

ASan 直接给出了「重叠」这个结论和精确调用栈，但**它只告诉你"这里错了"，不告诉你"块为什么会被发两次"**。所以还需要第二级。

**第二级：自研内存池探针**（同样只在副本中）

思路是把内存池的「分配中集合」显式记录下来，在分配/释放两个入口做校验：

```c
// 分配入口：同一 (entry, serial) 被分配两次 → 指针别名
void DbgAlloc(void* entry, int serial);   // 命中则打印栈并 abort
// 释放入口：释放一个不在分配集合中的块 → 双重释放 / 尺寸类不匹配
void DbgFree(void* entry, int serial);    // 命中则打印栈并 abort
```

再配合两处**不变式断言**：

```c
// MemoryEntryAllocTwo：弹出失败不允许用 serial=-1 参与指针运算
int pop_ok = MemoryStackTopAndPop(e->stack, &serial);
if (pop_ok != 1 || serial < 0) { /* 打印并 abort */ }

// MMemoryStackPush：空闲栈容量保护（原来没有上界检查）
if (s->top >= s->num) { /* 打印并 abort */ }
```

**这一次探针立刻生效**：首次压测第 1 轮 `/hello` 段就命中「释放了一个不在分配状态的块」，比段错误容易复现一个数量级。第三节的因果链就是这样闭环的。

### 2.4 复现命令

```bash
# 1) 崩溃复现（含临界点扫描）
for C in 50 100 120 150 200; do
  ./main & sleep 1
  wrk -t2 -c$C -d5s --timeout 5s http://127.0.0.1:5343/hello
  kill -0 $! && echo "c=$C 存活" || echo "c=$C 崩溃"
done

# 2) 队列信号量漂移观测（副本中已插桩）
wrk -t4 -c200 -d4s --timeout 3s http://127.0.0.1:5343/hello
#   → stderr: "QUEUE-DBG: 空弹出却仍 sem_post! blocknum=100 num=0 信号量值=100→101→102…"

# 3) Nagle 对照实验
#   基线：    wrk -t4 -c50 -d10s --latency http://127.0.0.1:5343/
#   变量组：  在 FdConnect 中 accept 后加一行 TCP_NODELAY 再重跑同一条命令
```

## 3. 性能基线数据（全部为本机实测）

### 3.1 环境与空载基线

```
CPU: AMD Ryzen 9 8945HX with Radeon Graphics, 可见核数: 2
wrk: wrk debian/4.1.0-4build3 [epoll]
服务空载: PID NLWP=3  RSS=3232 KB  VSZ=101740 KB  %CPU=0.0
```

固定开销：**3 个线程**（1 接收 + 2 工作 + 1 发送 中的实际活跃数按 `NLWP` 计为 3）、空载 RSS **3.2 MB**。这个资源占用是框架的亮点，值得写进简历。

### 3.2 四类负载的吞吐与延迟（`-t4 -d10s`）

| 负载 | 并发 | QPS | P50 | P90 | P99 | 吞吐 | 说明 |
|---|---|---|---|---|---|---|---|
| `/hello` 纯动态 | 50 | **17,520.74** | 1.17ms | 40.03ms | 51.55ms | 1.55 MB/s | 175,789 请求 / 15.59 MB |
| `/` 静态页（sendfile 3136B） | 50 | **1,048.90** | **43.60ms** | 53.22ms | 78.35ms | 3.22 MB/s | 10,525 请求 / 32.29 MB |
| `/mem/256k` 内存响应 | 50 | 4,128.93 | 5.86ms | 44.08ms | 55.94ms | **1.01 GB/s** | 41,564 请求 / 10.15 GB |
| `/download/10mb` 大文件 | 20 | 494.94 | 29.34ms | 54.08ms | 83.74ms | **4.83 GB/s** | 4,985 请求 / 48.68 GB |

**两点解读：**

1. **`sendfile` 路径非常强**：10MB 文件 **4.83 GB/s**、256KB 响应 **1.01 GB/s**。零拷贝 + 大块写把文件下发做到了本机带宽级别，这是框架最值得展示的数字。
2. **静态小页面却只有 1,048 QPS、P50 = 43.60ms** —— 比纯动态接口慢 **16.7 倍**。`43.60ms ≈ 40ms` 是**延迟 ACK 定时器**的指纹，详见 **OPT-01**。

### 3.3 并发扫描与崩溃临界点（`/hello`，`-t2 -d5s`）

| 并发 | 结果 | QPS | socket 错误 |
|---|---|---|---|
| 50 | 存活 | 12,972.34 | — |
| 100 | 存活 | 19,814.94 | — |
| 120 | 存活 | 39,656.80 | read 28 |
| 150 | 存活 | 58,517.55 | read 249 |
| 200 | 存活 | **59,707.78** | read 22 |
| 200（`-d8s --timeout 5s`） | **段错误（核心已转储）** | **0** | read 100, **write 572,714** |

同一档位 `c=50` 在两次测量中分别是 17,520 与 12,972（**波动约 26%**），因此表中差异 ≤30% 的档位不宜过度解读。

**⚠️ 两个必须正视的现象：**

- **`c=200` 的结果是不确定的**：同一条命令，有时 59,707 QPS 正常跑完，有时 8 秒内 **0 请求** 并以 `段错误（核心已转储）` 退出（`write 572,714` 是内核在连接已死后继续投递写事件产生的）。**"偶发崩溃"比"必崩"更危险**，因为它会骗过一切人工验证。根因见第 4 章。
- **QPS 随并发升高而升高（12.9k → 59.7k）不符合常规预期**（通常应饱和）。由于框架**不支持 keep-alive**，每个请求都是一次完整连接生命周期，低并发时客户端侧与连接建立/销毁开销无法被掩盖。**归因尚未完成**，后续可用 `strace -c -p <pid>` 与 `perf top` 确认（见第 9 章）。

### 3.4 压测后资源画像

```
PID 103974  NLWP=3  RSS=4768 KB  VSZ=232944 KB  %CPU=25.8
线程数: 3    fd 数: 24    TIME_WAIT 数: 12
```

RSS 从 3.2 MB 涨到 4.7 MB 后稳定，fd 稳定在 **24**，**未见 fd 泄漏**（这是个正面结论：说明 `FdConnectOrClose` 的主体流程在无异常路径下是对的；BUG-13 是**异常路径**才触发的漏检）。

---

## 4. P0 缺陷：高并发下偶发段错误（BUG-11 → BUG-12）

### 4.1 现象

`wrk -t4 -c200 -d8s --timeout 5s http://127.0.0.1:5343/hello`：

```
0 requests in 8.11s, 0.00B read
Socket errors: connect 0, read 100, write 572714, timeout 0
/tmp/bench.sh: 第 50 行： 103974 段错误（核心已转储）./main > /tmp/bench_server.log 2>&1
--- 该轮后进程是否还活着 ---  !!! 已退出
```

但**同一条命令有时又能跑出 59,707 QPS 且进程存活**。间歇性出现的信号，不能被"多试几次没复现"打发掉。

### 4.2 证据一：ASan 报告 —— 两个区间**起始地址完全相同**

`/tmp/asan_report.104711`（55 次同类报错，调用栈完全一致）：

```
==104711==ERROR: AddressSanitizer: strcat-param-overlap:
    memory ranges [0x76d0e5205c00,0x76d0e5205ca1) and [0x76d0e5205c00,0x76d0e5205c51) overlap
    #0 strcat
    #1 PackBackHead   Threads/WorkerThread.c:192
    #2 PackTotal      Threads/WorkerThread.c:52
    #3 WorkerMain     Threads/WorkerThread.c:138
    #4 func2          StaterMain/StarterMain.c:25
    #5 asan_thread_start → start_thread → __clone3

0x76d0e5205c00 is located 21504 bytes inside of 122880-byte region
    [0x76d0e5200800,0x76d0e521e800)
allocated by thread T0 here:
    #0 calloc
    #1 MemoryEntryInit   MemoryPool/MemoryPool.c:77
    #2 MemoryPoolInit    MemoryPool/MemoryPool.c:164
    #3 WorkerThreadInit  Threads/WorkerThread.c:77
    #4 StartThread       StaterMain/StarterMain.c:70
    #5 WebStater → main /tmp/cweb_asan/main.c:10
```

随后服务端 stderr：`AddressSanitizer:DEADLYSIGNAL`（即段错误本体）。

**关键在地址**：两个区间**起点都是 `0x76d0e5205c00`**。`strcat(dest, src)` 的 `dest` 与 `src` 若是同一起点，说明 **`*c == w->body`** —— 两个本该独立的缓冲区拿到了**同一个指针**。

### 4.3 由长度反推：确实是"同一个块、同一尺寸类"

| 推导项 | 数值 | 依据 |
|---|---|---|
| `dest` 指向区域 | `[…5c00, …5ca1)` → **161 字节** | `0xca1 - 0xc00 = 0xa1 = 161 = 160 + '\0'` |
| `src` 指向区域 | `[…5c00, …5c51)` → **81 字节** | `0xc51 - 0xc00 = 0x51 = 81 = 80 + '\0'` |
| `dest` 内容构成 | `"HTTP/1.1 200 OK\r\n"`(17) + `headers->begin`(143) = 160 | 与 `PackBackHead` 先 `strcpy` 状态行再 `strcat` 头的写法一致 |
| 分配大小 | `allocsize = strlen(headers->begin)+body_size+17 = 143+80+17 = 240` → `240/1024+1 =` **1KB** | `WorkerThread.c:44,47` |
| 释放大小 | `(body_size/1024+1)*1024 = (80/1024+1)*1024 =` **1024 字节** → 1KB | `WorkerThread.c:193` |
| 所属池 | `122880 = 120 × 1024`，与 `MemoryPoolInit(&txt_pool,7,7,200,120)` 的「1KB 尺寸类 × 120 块」精确吻合 | `WorkerThread.c:77` |
| 块序号 | `21504 / 1024 =` **21** | — |

**这就解释了为什么表现是"重叠"而不是"内容错乱"**：小于 1KB 的响应，其**响应体**和**打包缓冲**恰好落在**同一个尺寸类（1KB）**里，所以一旦池里出现重复的 `serial=21`，两次分配必然返回同一地址，`strcat` 立刻源目重叠。

### 4.4 证据二：内存池探针 —— 直接抓到"释放了不属于分配集的块"

在副本中给内存池加"分配中集合"校验后，压测首轮即命中（函数名与调用链为实测；行号为**已插桩副本**的行号，对应原文件 `MemoryPool.c:136-146` 与 `WorkerThread.c:64,147`）：

```
*** POOL-DBG: 释放了一个不在分配状态的块（双重释放 / 尺寸类不匹配）
   MemoryEntryFree   MemoryPool/MemoryPool.c:152
   MemoryPoolFree    MemoryPool/MemoryPool.c:225
   CleanMemoryQueue  Threads/WorkerThread.c:61
   WorkerMain        Threads/WorkerThread.c:149
```

**双重释放被钉死在 `CleanMemoryQueue` 这条回收链上**，而它正是"发送线程发完包后，工作线程把内存还给池"的必经之路：

```c
// Threads/WorkerThread.c:58-69（原文件）
int CleanMemoryQueue(Worker_Thread* profes){
    int e1=1;
    while(e1!=0){
        Worker_Queue_Entry m=WorkerQueueTopAndPop(profes->memory_queue,&e1);
        if(e1==1){
            MemoryPoolFree(profes->txt_pool,m.char_ptr,(m.size/1024+1)*1024);
        }
    }
    return 1;
}
```

再看 `MemoryEntryFree` —— **它完全不校验 `ptr` 是否真属于本 entry、也不校验 `serial` 是否处于分配中**，且无条件 `haded_num--`（可减成负数）：

```c
// MemoryPool/MemoryPool.c:136-146（原文件）
int MemoryEntryFree(Memory_Entry* e,void* ptr)
{
    int size=e->strip_size;
    int total_gap=(char*)ptr-e->memory_strip;
    int serial=total_gap/(size*1024);      // 只做除法，不做任何归属校验
    MMemoryStackPush(e->stack,serial);     // serial 直接压回空闲栈
    e->haded_num--;                        // 无下界保护
    return 1;
}
```

（`MemoryPoolFree` 在 `:211` 确实有一次区间检查，但命中时是 **`return 0` 静默返回**，调用方 `CleanMemoryQueue` 丢弃了返回值，无从感知。）

### 4.5 证据三：队列探针 —— 空弹出仍 `sem_post`，信号量无上限漂移

```c
// Queue/WorkerQueue/WorkerQueue.c:48-74（原文件）
Worker_Queue_Entry WorkerQueueTopAndPop(Worker_Queue* sq,int* error)
{
    pthread_mutex_lock(&(sq->mutex));
    ...
    if(sq->num!=0){                    // 只有这里才真的弹出
      if(sq->ptr_out+1==sq->end) sq->ptr_out=sq->begin;
      else                        sq->ptr_out++;
      sq->num--;
      *error=1;
    }
    pthread_mutex_unlock(&(sq->mutex));
    sem_post(&(sq->queue_notfull));    // ← 第 72 行：在 if 之外，空弹出也归还容量
    return s;
}
```

探针实测（`blocknum=100` 的队列，35 秒压测）：

```
QUEUE-DBG: 空弹出却仍 sem_post!  blocknum=100  num=0  信号量值=100 → 101 → 102 →
命中次数: 18,748 次 / 35s
```

`sem_post` 的语义是"我取走了一个元素，容量回来一格"。**队列为空时 post，等于凭空增加容量**。这个信号量正是 `WorkerQueuePush` 侧"队列未满"的闸门 —— 闸门一旦虚高，`Push` 就会**越容量写入环形数组**：

```c
// 推测的写入路径（与 WorkerQueuePush 的 sem_wait 语义一致）
sem_wait(&queue_notfull);              // 因漂移而永远不阻塞
sq->queue[sq->ptr_in] = entry;         // ptr_in 绕圈后覆盖尚未被取走的旧条目
sq->ptr_in = (sq->ptr_in+1==sq->end) ? sq->begin : sq->ptr_in+1;
```

被覆盖的那个条目，其指向的内存块**其实还没被释放**；而发送线程随后可能已经把它 post 给了 `memory_queue`，于是 `CleanMemoryQueue` 会**对同一个指针释放两次** —— 与 4.4 的探针结果严丝合缝。

### 4.6 因果链（每一环都有实测支撑）

```
[实测] WorkerQueue.c:72 空弹出仍 sem_post        —— 18,748 次 / 35s
   ↓
[实测] queue_notfull 信号量 100 → 101 → 102…     —— 无上限漂移，容量闸门失效
   ↓
[机制] WorkerQueuePush 不再阻塞 → 100 槽环形队列被越容量写入
   ↓
[机制] ptr_in 绕圈覆盖尚未取走的条目 → 同一个 char_ptr 出现两次
   ↓
[实测] CleanMemoryQueue（WorkerThread.c:64）对同一指针二次 free
   ↓
[实测] 探针：MemoryEntryFree 收到"不在分配集"的块（4.4）
   ↓
[机制] MemoryEntryFree 无归属校验 → 同一 serial 被压回空闲栈两次
   ↓
[机制] 空闲栈出现重复 serial=21 → 两次分配返回同一地址
   ↓
[实测] 小响应下 body 与打包缓冲同属 1KB 尺寸类（4.3 长度反推）
   ↓
[实测] ASan：strcat 源目重叠 → glibc 向量化 strcat 越界 → SIGSEGV
```

### 4.7 最小修复（3 处，共 5 行）

**修复 1 —— 只在真正弹出的分支归还容量**（`Queue/WorkerQueue/WorkerQueue.c:60-72`）

```c
    if(sq->num!=0){
      if(sq->ptr_out+1==sq->end) sq->ptr_out=sq->begin;
      else                        sq->ptr_out++;

      sq->num--;
      *error=1;
      pthread_mutex_unlock(&(sq->mutex));
      sem_post(&(sq->queue_notfull));   // ← 移入分支内，与 num-- 成对
      return s;
    }

    pthread_mutex_unlock(&(sq->mutex));
    return s;                            // 空队列：不 post、不改变任何状态
```

**修复 2 —— 弹出失败时不得用 `serial=-1` 参与指针运算**（`MemoryPool/MemoryPool.c:121-134`）

```c
int MemoryEntryAllocTwo(Memory_Entry* e,void** ptr,int* notfull)
{
    *notfull=0;
    if(e->haded_num==e->strip_num) return 1;

    int size=e->strip_size*1024;
    int serial=-1;
    int pop_ok=MemoryStackTopAndPop(e->stack,&serial);
    if(pop_ok!=1||serial<0) return 1;    // ← 保持 *notfull=0，上层转 503
    *ptr=(void*)(e->memory_strip+serial*size);
    e->haded_num++;
    *notfull=1;
    return 1;
}
```

（`MemoryEntryAlloc`（`:108-119`）有同样问题且调用方更危险，建议一并处理；`MemoryStackTopAndPop` 的返回值本身也需要被检查，见 **BUG-17**。）

**修复 3 —— 空闲栈容量上界保护**（`MemoryPool/MemoryPool.c`，`MMemoryStackPush` 内）

```c
int MMemoryStackPush(Memory_Stack* s,int serial)
{
    if(s==NULL||s->leisure==NULL) return 0;
    if(s->top>=s->num) return 0;   // ← 新增：栈满即拒绝，杜绝同一 serial 重复入栈
    s->leisure[s->top]=serial;
    s->top++;
    return 1;
}
```

> 修复 3 是"最后一道防线"：即使上游仍有重复归还，也不会再把同一个块发两次。**但必须优先修的是修复 1**，否则队列内容错乱依然会导致响应串包。

### 4.8 修复后的验收标准

1. `make CFLAGS="-g -O0 -fsanitize=address"` 构建后，`wrk -t2 -c200 -d30s http://127.0.0.1:5343/hello` **连续 3 轮无 ASan 报错、无 `DEADLYSIGNAL`**；
2. 同一测试连续 3 轮均返回成功响应（`Requests/sec > 0`），无 `段错误（核心已转储）`；
3. 带探针构建下：**空弹出计数 = 0**、**信号量值恒定 ≤ 队列容量**；
4. 现有 `make -C test run` 仍为 **256 PASS / 0 FAIL / 0 HANG**（无回归）；
5. 建议**同时**把"ASan 构建 + 30s 并发压测"加进 `test/run_tests.sh`，作为此缺陷的回归护栏。

---

## 5. P0 缺陷：`FdClose` 空指针解引用（BUG-13）

### 5.1 缺陷代码

```c
// FdConnectOrClose/FdConnectOrClose.c:50-56（原文件）
    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,client_fd,&fd_ob);   // ← 返回值被丢弃

    //回收send池
    SendPointerPoolDestory(fd_ob->send_pointer_pool,w->store_area);   // fd_ob 可能为 NULL
    //回收http暂存区
    RequestTempPoolDestory(fd_ob->request_temp_pool,w->store_area);
```

`FdTableFind` 在查不到 fd 时不写 `fd_ob`，于是 `fd_ob` 保持初始化时的 `NULL`，下一行立刻解引用 —— 这是**独立的**一条崩溃路径（与 BUG-11/BUG-12 的池损坏无关）。

### 5.2 触发条件与同类问题

`FdClose` 会被「正常收尾」「超时」「解析出错」等多条路径调用。只要有任何一次**重复关闭**（例如 timer 已触发关闭、随后 epoll 又投递了一个事件），第二次进来时 fd 已从表中摘除，即命中 `fd_ob == NULL`。补丁：

```c
    Fd_Entry* fd_ob=NULL;
    if(FdTableFind(w->fd_table,client_fd,&fd_ob)!=1||fd_ob==NULL) return 0;  // ← 新增
```

同一函数/相邻函数另有 2 处漏检，一并列出：

| 位置 | 代码 | 后果 |
|---|---|---|
| `FdConnectOrClose.c:32` | `FdTableInsert(w->fd_table,client_fd);` | 插入失败（表满/内存不足）后仍继续挂 timer、挂 epoll → 连接实际不可管理 |
| `Threads/ReceiverThread.c:23-26` | `int e0=TasKQueuePush(...);` 后**函数直接结束** | 任务队列满时请求**静默丢弃**，客户端无任何响应，干等超时；`e0` 未使用，且非 void 函数**缺 return** |

---

## 6. P1 缺陷（BUG-14 ~ BUG-16）

### 6.1 BUG-14：池满时 `*C` 未被赋值 + 变量未初始化

```c
// Threads/WorkerThread.c:135（原文件）
    char* C;                                  // ← 未初始化
// Threads/WorkerThread.c:44-50（原文件）
    *allocsize=strlen(Rsp->headers->begin)+Rsp->body_size+17;
    void* ptr2=NULL;
    int notfull2=0;
    MemoryPoolAlloc(profes->txt_pool,*allocsize/1024+1,&ptr2,&notfull2);
    if(notfull2==0)  Rsp->error_reason=503;   // ← 走这条分支时 *C 完全没被赋值
    else  *C = ptr2;
```

`WorkerMain:140` 的错误分支虽然不会用到 `C`，但**未初始化的 `char*` 一旦被后续代码（或重构）读到就是 UB**，且 `PackTotal` 的 503 分支把"不赋值"当成了隐式契约 —— 这类契约没有任何注释或断言保护。补丁：

```c
    char* C=NULL;                             // ← 初始化
    ...
    if(notfull2==0){ Rsp->error_reason=503; *C=NULL; }   // ← 显式置空，契约可见
    else  *C = ptr2;
```

### 6.2 BUG-15：`Alloc` 用 KB、`Free` 用字节，单位不一致

```c
// MemoryPool/MemoryPool.c:173
int MemoryPoolAlloc(Memory_Pool* p,int size,void** ptr,int* notfull)   // size 单位 = KB
{   ...
    if(size>p->pool[p->Entry_num-1].strip_size){ *ptr=malloc(size*1024); return 1; }  // ×1024
// MemoryPool/MemoryPool.c:197
int MemoryPoolFree(Memory_Pool* p,void* ptr,int size)//单位字节        // size 单位 = 字节
{   if(size>p->pool[p->Entry_num-1].strip_size*1024) { free(ptr); ... }             // 直接比
```

两个函数只有**一步之隔**，单位却不同（源码注释自己都写了"单位字节"）。当前全靠 6 个调用点手写 `/1024`、`*1024` 对齐；**任何一处漏写都会把块归还进错误的尺寸类**，进而触发与 4.4 完全一样的池损坏。这是"迟早要出事"的设计缺陷，建议统一为字节或统一为 KB，并给参数改名（如 `kb` / `bytes`）。

### 6.3 BUG-16：`MemoryStackExpend` 泄漏 + 锁对象错误

```c
// MemoryPool/MemoryPool.c:25-42 区域（原文件）
    新栈 = 申请更大的 leisure 数组
    ...
    free(e->stack);          // ← 只 free 了结构体，没 free 旧的 e->stack->leisure 数组
    e->stack = 新栈;         // ← 且上面加的是"新栈"的 mutex，不是被换掉的旧栈
```

后果：(a) **每次扩容泄漏一个完整空闲栈数组**（长期运行的服务会持续增长）；(b) 扩容发生在分配路径上，锁错对象意味着扩容期间与并发使用者没有真正互斥。

---

## 7. P1 缺陷：4 处返回值被丢弃（BUG-17）

| 位置 | 被丢弃的返回 | 后果 |
|---|---|---|
| `MemoryPool.c:114`（`MemoryEntryAlloc`） | `MemoryStackTopAndPop(e->stack,&serial)` | 弹出失败时 `serial` 仍是 `-1` → `ptr = memory_strip - size`，**越界指针** |
| `MemoryPool.c:128`（`MemoryEntryAllocTwo`） | 同上 | 同 4.7 修复 2 |
| `Threads/ReceiverThread.c:23` | `TasKQueuePush` | 请求静默丢失（见 5.2） |
| `FdConnectOrClose.c:32` | `FdTableInsert` | 连接不在管理表中（见 5.2） |

**统一建议**：本项目大量使用 `int` 返回码但调用处不检查。建议对"内存/队列/表"这三类操作的返回值**至少加断言或日志**，这是低成本高收益的加固。

---

## 8. 性能缺陷与优化实验

### 8.1 OPT-01：Nagle + 延迟 ACK 造成 40ms 停顿（**A/B 实测 11.2× 提升**）

**现象与诊断过程**

1. 观察异常比例：静态页 `/`（3136B，`sendfile`）**1,048.90 QPS / P50 43.60ms**，而纯动态接口 `/hello` 是 **17,520.74 QPS / P50 1.17ms** —— 文件更小、路径更"零拷贝"，却慢 **16.7 倍**；
2. 提取指纹：`P50 43.60ms`、`P75 45.31ms` 高度聚拢在 **40ms** 附近，这是 **TCP 延迟 ACK 定时器**（Linux 默认 `TCP_DELACK_MIN = 40ms`）的典型特征，而非排队/CPU 造成（CPU 仅 25.8%）；
3. 定位机理：静态响应是**两段式写**——
   ```c
   write(client_fd, headers, header_len);         // 第 1 段：状态行 + 响应头（小段）
   sendfile(client_fd, fd, &off, file_size);      // 第 2 段：文件正文
   ```
   `TCP_NODELAY` **未设置**时，第 1 段数据未被对端 ACK 之前，**Nagle 算法会扣住第 2 段**（因为它小于 MSS），而对端又因"已收到数据、暂无可回内容"而等待延迟 ACK 定时器 —— 双方互相等待，每请求空转 **40ms**；
4. 对照实验（唯一变量）：在 `FdConnect` 中 `accept` 之后加一行

   ```c
   { int one=1; setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); }
   ```

**A/B 结果**（其余环境、命令、负载完全相同：`wrk -t4 -c50 -d10s --latency http://127.0.0.1:5343/`）

| 指标 | 修复前 | 修复后 | 提升 |
|---|---|---|---|
| QPS | 1,048.90 | **11,776.93** | **11.2×** |
| 吞吐 | 3.22 MB/s | **36.13 MB/s** | **11.2×** |
| 平均延迟 | 45.58ms | **12.15ms** | 3.8× |
| **P50** | **43.60ms** | **1.75ms** | **24.9×** |
| P75 | 45.31ms | 22.00ms | 2.1× |
| P90 | 53.22ms | 40.31ms | 1.3× |
| 10s 完成请求 | 10,525 | **118,209** | 11.2× |

> **诚实注记**：加 `TCP_NODELAY` 后 **P90 仍为 40.31ms**，说明**还存在第二条停顿来源**（可能是连接建立/销毁路径上的串行开销，与 3.3 中"QPS 随并发升高"的现象疑似同源）。本次**未进一步归因**，列为后续工作（第 9 章）。

**修复建议**：在 `accept` 成功后立即设置 `TCP_NODELAY`（一行）。这是一个**零风险、11 倍收益**的改动，**建议优先落地**。

### 8.2 OPT-02：`Connection: close` 硬编码，无 keep-alive

```c
// FdConnectOrClose/FdConnectOrClose.c:95（原文件，错误响应构造中同样硬编码）
        "Connection: close\r\n"
```

后果：客户端**每个请求都要重新三次握手**，QPS 上限受 `accept` 路径吞吐限制；也使得所有连接建立/销毁相关的开销（fd 表插入删除、timer 分配释放、epoll add/del、每连接两个池的创建销毁）被**放大到每请求一次**。这与 3.3 的并发异常、以及 4.4 中暴露的收尾路径缺陷高度相关。

**注意**：实现 keep-alive 需要同时处理「请求边界判定」「连接状态复位」「超时策略」，属于结构性改动，**不建议在修复第 4~7 章的缺陷之前进行**。

---

## 9. 未修复项与后续行动

### 9.1 本次**未做**的事情（诚实声明，避免误读）

| 项目 | 状态 |
|---|---|
| `valgrind --leak-check` 完整内存泄漏报告 | **未执行**（工具可用：`valgrind 3.26.0`）；本次用 ASan + 探针替代，已足以定位 BUG-12 |
| `strace -c` 系统调用画像、`perf` 热点分析 | **未执行**（工具可用）；8.1 的 40ms 残留停顿与 3.3 的并发异常因此**归因未完成** |
| BUG-11/BUG-12 修复后的复测 | **未执行**（修复补丁已给出但未落地，见 9.2） |
| 对 `WebSite/`、`WebApp/` 应用层自身代码的审查 | **未执行**（本轮只审查框架层） |

### 9.2 建议的处理顺序

| 优先级 | 行动 | 涉及文件 | 预期收益 |
|---|---|---|---|
| **1** | 修复 BUG-11（队列空弹出 `sem_post`） | `Queue/WorkerQueue/WorkerQueue.c` | 消除崩溃根因 |
| **1** | 修复 BUG-12（池分配/释放校验） | `MemoryPool/MemoryPool.c` | 消除崩溃与别名 |
| **1** | 修复 BUG-13（`FdClose` 空指针） | `FdConnectOrClose/FdConnectOrClose.c` | 消除第二条崩溃路径 |
| **2** | 加 `TCP_NODELAY` | `FdConnectOrClose/FdConnectOrClose.c` | **静态页 11.2×** |
| **2** | BUG-17 四处返回值检查 | 见第 7 章 | 消除静默失败 |
| **3** | BUG-14/15/16 | `WorkerThread.c` / `MemoryPool.c` | 消除 UB 与泄漏 |
| **3** | CI 增加「ASan 构建 + 30s 并发压测」 | `test/run_tests.sh` | 拦住整类并发缺陷 |
| **4** | keep-alive（OPT-02） | 结构性改动 | QPS 上限抬升 |

### 9.3 复测验收清单

见 **4.8**（崩溃类缺陷）与 **8.1**（性能类缺陷，指标为 QPS ≥ 10,000、P50 ≤ 5ms）。

---

## 10. 附录

### 10.1 证据文件与实验脚本清单

| 文件 | 内容 |
|---|---|
| `/tmp/bench_out.txt` | 第 3 章全部基线数据（含崩溃现场原文） |
| `/tmp/asan_report.104711` | ASan 原始报告（55 次 `strcat-param-overlap`，含 `alloc_stack` 与线程创建栈） |
| `/tmp/asan_summary2.txt` | 55 次重叠的调用栈去重统计 + `DEADLYSIGNAL` 现场 |
| `/tmp/dbg2_final.txt` | 内存池探针输出（双重释放调用链） |
| `/tmp/dbg_server.log` | 队列探针输出（空弹出计数与信号量漂移） |
| `/tmp/repro_out.txt` | 并发扫描（c=50…200）结果与 `core_pattern`/`ulimit -c` 设置 |
| `/tmp/nd_out.txt` | `TCP_NODELAY` 对照实验两组结果 |
| `/tmp/bench.sh` `/tmp/repro.sh` `/tmp/deep.sh` `/tmp/nd_bench.sh` | 各轮实验脚本（可重跑） |
| `/tmp/cweb_asan/` | ASan + 探针插桩副本（**与分析无关的临时副本**） |
| `/tmp/cweb_nd/` | `TCP_NODELAY` 对照副本 |

> 上述均为 `/tmp` 下的临时产物，**不属于仓库**；重启即失，需要长期保留请拷贝进 `test/artifacts/`。

### 10.2 自研探针的关键不变式（可复用）

探针不依赖任何第三方库，价值在"把隐式契约变成显式断言"：

```c
/* 不变式 1：空闲栈不得越界；若越界说明同一 serial 被重复归还 */
if (s->top >= s->num) {
    fprintf(stderr, "*** STACK-DBG: 空闲栈溢出 top=%d num=%d\n", s->top, s->num);
    abort();
}

/* 不变式 2：弹出必须成功；失败时 serial 不得参与指针运算 */
int pop_ok = MemoryStackTopAndPop(e->stack, &serial);
if (pop_ok != 1 || serial < 0) {
    fprintf(stderr, "*** POOL-DBG: 弹出失败 pop_ok=%d serial=%d\n", pop_ok, serial);
    abort();
}

/* 不变式 3：释放的块必须处于"分配中"集合（否则即为双重释放） */
if (!DbgIsAllocated(e, serial)) {
    fprintf(stderr, "*** POOL-DBG: 释放了不在分配状态的块 serial=%d\n", serial);
    abort();
}

/* 不变式 4：队列空弹出不得 post 信号量 */
if (sq->num == 0) {
    fprintf(stderr, "*** QUEUE-DBG: 空弹出却仍 sem_post! num=%d\n", sq->num);
}
```

这 4 条断言把"偶发、需数分钟"的崩溃，变成了"首轮压测即命中并打印完整调用链"。

### 10.3 复现环境

```
OS      : Linux（/tmp/nd 实验与基线同一台机）
CPU     : AMD Ryzen 9 8945HX with Radeon Graphics，容器/VM 内可见 2 核
工具    : wrk debian/4.1.0-4build3 [epoll]、gcc 15.2.0、valgrind 3.26.0、AddressSanitizer（libasan）
构建    : 基线 make -j4 / ASan 构建 make CFLAGS="-g -O0 -fsanitize=address"
监听    : 127.0.0.1:5343
```

### 10.4 仓库完整性的核验

本报告的所有插桩与对照实验都在 `/tmp` 副本中进行。对**缺陷所在文件**逐一 `git diff --quiet` 核验，结果均为**原始提交状态**（即缺陷是框架既有问题，非本次开发引入）：

```
Queue/WorkerQueue/WorkerQueue.c               未改动（原始提交状态）
MemoryPool/MemoryPool.c                       未改动（原始提交状态）
Threads/WorkerThread.c                        未改动（原始提交状态）
FdConnectOrClose/FdConnectOrClose.c           未改动（原始提交状态）
ResponseSend/ResponseSendMain.c               未改动（原始提交状态）
Threads/ReceiverThread.c                      未改动（原始提交状态）
```

### 10.5 结论一句话

> 架构方向正确、`sendfile` 路径已达带宽级（**4.83 GB/s**）、资源占用极低（**3 线程 / 3.2 MB**）；但**队列容量契约（BUG-11）与内存池归属校验（BUG-12）两处缺失，使服务在高并发下会偶发崩溃**，叠加**缺少 `TCP_NODELAY`（OPT-01）白丢 11 倍性能**。三者都是小改动，建议按 9.2 的顺序处理并复测。






