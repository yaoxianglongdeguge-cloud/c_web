# c_web HTTP 服务器 测试报告

## 0. 报告信息

| 项目 | 内容 |
|---|---|
| 被测版本 | `314174f`（分支 `main`），工作区**未做任何源码修改**（`git status` 无已跟踪文件改动） |
| 测试日期 | 2026-10-13 |
| 测试环境 | Linux 7.0.0-31-generic / x86_64 / gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| 测试对象 | 接收线程池 + 请求暂存区/完整性判定 + 请求解析 + 路由 + 响应构造/发送 + 定时器 + 内存池/队列/锁 |
| 测试代码位置 | `test/`（11 个用例，全部新增，不参与主程序构建） |
| 执行方式 | `make -C test run`（单元/组件级）；原版服务二进制 + curl/ss（端到端）；gdb（崩溃定位） |
| 总体结论 | **不建议当前版本对外提供服务**：存在 3 个阻断级（P0）缺陷，服务实际无法处理任何 HTTP 请求；另有 2 个功能级（P1）、4 个次要（P2）问题 |

> **⚠️ 2026-10-13 22:58 复测更新（第二版）**：被测代码已被修改 4 个文件（见 **9.1**）。修复后复测结果：**229 PASS / 0 FAIL / 0 崩溃，但 2 个用例卡死（HANG）**，并且**新引入 1 个 P0 回归 —— BUG-05R：查询串切分死循环（带 `?` 的请求即 DoS）**。BUG-01、BUG-02、BUG-03、BUG-04、BUG-06 已确认修复。详见 **第 9 章**。第 1~8 章保留为 `314174f` 原版基线的记录，未作修改。

> **✅ 2026-10-14 复测更新（第三版）**：`RequestAnalysiser.c` 再改一次后（见 **10.1**）复测结果：**254 PASS / 0 FAIL / 0 HANG，11/11 用例全绿（RUN_EXIT=0）**，BUG-05R 的**死循环已修复**（`GET /?a=1` 从 5s 无响应 + 100% CPU → `404 / 0.05s`，查询参数也能正确解析）。**残留 1 个 P0**：`GET /?` / `GET /?&` 这类"空查询串/纯分隔符"会让**整个进程段错误退出**（一行可修）。详见 **第 10 章**。

> **❌ 2026-10-14 复测更新（第四版）**：新增的 `if(query_ing == "") return 1;` 是**指针比较**，永远不成立（编译器已给出 `-Waddress` 告警），P0 崩溃**依然存在**（`GET /?`、`GET /?&`、`GET /?&&` 三次实测都 core dump），套件仍是 254 PASS / 2 FAIL / 0 HANG。**正确的最小补丁是在首个 `strtok_r` 之后加 `if (line2 == NULL) return 1;`**（注意仅判 `*query_ing=='\0'` 挡不住 `/?&`）。详见 **第 11 章**。

> **✅ 2026-10-14 复测更新（第五版，当前工作区状态）**：按 11.4 加入 `if(line2 == NULL) return 1;` 后，**P0 全部清零** —— **256 PASS / 0 FAIL / 0 HANG，11/11 用例全绿（RUN_EXIT=0）**，端到端 10 个原始 TCP 探针（`/`、`/?`、`/?&`、`/?&&`、`/?&&&`、`/?a=1`、`/?a=1&b=2`、`/?=`、`/?;`、`/?%20`）全部正常回 `404` 且进程存活，30 轮 `/?`+`/?&` 混合压测后进程仍存活。**结论升级为：可用于功能联调/内测**；正式对外前建议先处理 BUG-07（业务路由未注册，所有请求都是 404）。详见 **第 12 章**。

> **✅ 2026-10-14 新增第 13 章（示例网站与应用层，当前工作区状态）**：本轮**新增** `WebSite/`（静态网站：`index.html` 含 `c_web is running`、`about.html`、`style.css`、`app.js`、`favicon.ico`、`static/10mb.bin`）与 `WebApp/`（`WebApp.c/.h`，注册 14 条路由），并把 `main.c` 的坏桩替换为 `WebStater("127.0.0.1",5343,WebAppRegisterRoutes)`、把根 `Makefile` 的 `wildcard` 结果排除 `test/%`。结果：**BUG-07 修复**（所有请求 404 → 路由可用）、**BUG-09 修复**（根 `make` 由 rc=2/multiple definition 变为 rc=0）、**网站端到端 27 PASS / 0 FAIL**、**单元套件 256 PASS / 0 FAIL / 0 HANG 未回归**，200 次并发混合请求字节级对账一致。详见 **第 13 章**。

> **📄 2026-10-14 新增配套报告（并发压力与缺陷定位，`test/STRESS_TEST_REPORT.md`）**：对「接收 → 任务队列 → 工作线程 → 内存池打包 → 接收队列 → 发送线程 → 内存回收」这条全并发链路做了 `wrk` 持续压测 + ASan + 自研探针定位，**新增 7 个缺陷（BUG-11 ~ BUG-17，编号与本文档连续，本文档止于 BUG-10）**，其中 3 个 P0：**BUG-11 队列空弹出仍 `sem_post` 导致容量闸门失效 → BUG-12 内存池双重释放并重复发放同一块 → `strcat` 源目重叠 → 高并发下偶发段错误**（`c=200` 时 8s 内 0 请求并 core dump）；另定位 **OPT-01 缺 `TCP_NODELAY` 导致静态页 43.60ms 停顿，A/B 实测 QPS 1,049 → 11,777（11.2×）**。本文档的 256 条断言属功能级测试，**结构上无法覆盖该类时序缺陷**（原因见该报告 2.1）。

---

## 1. 结论摘要

| 编号 | 级别 | 问题 | 位置 | 测试结果 |
|---|---|---|---|---|
| BUG-01 | **P0 阻断** | 监听 fd 未回传，接收线程 epoll 挂到 `-1`，连接被内核接受但永不处理 | `StaterMain/StarterMain.c:41-58` | 端到端：curl 5s 超时、0 字节 |
| BUG-02 | **P0 阻断** | `MethodMatch` 把**形参**置空后再解引用，首个请求即 SIGSEGV | `RequestCome/RequestIntergrity/RequestIntergrity.c:24,28` | `test_temp_pool` 崩溃(exit 139)、`test_integrity` FAIL |
| BUG-03 | **P0 阻断** | `EPOLLIN \|\| EPOLLOUT` 逻辑或塌缩为 `1`，发送端等不到可写事件 → 大响应/大文件下发中途永久挂起 | `FdConnectManage/EpollOperate/EpollOperate.c:30` | `test_epoll` 2 条 FAIL |
| BUG-04 | P1 功能 | `Content-Length` 被重复跳过 15 字节，长度解析为 0 → 413 失效、POST 体截断 | `RequestIntergrity.c:41`（调用方已跳过 `:107`） | `test_integrity` 2 条 FAIL |
| BUG-05 | P1 功能 | 查询串解析：`query_ing`（第 99 行算出的首个参数）被弃用，`line2=""` 起手 + 首次 `strtok_r(NULL,…)` → 一个 query 参数都解析不到 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:99,111-125` | `test_parser` 3 条 FAIL、`test_integrity` 1 条 FAIL |
| BUG-06 | P2 次要 | 非 void 函数 `StartThread` 缺 `return`，返回值是寄存器垃圾 | `StarterMain/StarterMain.c:60-82` | 编译警告 |
| BUG-07 | P2 次要 | 唯一 handler 使用 `printf("%c", "hello world")`（`char*` 传给 `%c`，UB），且**未注册任何路由** | `main.c:4` | 编译警告 |
| BUG-08 | P2 次要 | 分配结果 `e0` 未使用，内存分配失败未检查 | `RequestAnalysiser.c:19` | 编译警告 |
| BUG-09 | P2 集成 | 根 `Makefile` 通配会把 `test/src/*.c` 编进主程序 → 根目录 `make` 直接失败 | 根 `Makefile:17`（`SRCS = $(wildcard …)`） | `/tmp` 副本实测链接失败 |
| BUG-10 | P3 提示 | `-Wall` 下另有 8 处静态告警（其中 4 处为同类“非 void 函数缺 return”），编译器已就 BUG-03/BUG-05 直接告警 | 见 5.BUG-10 | 逐条列示 |

**测试统计（原版代码，11 个用例）**

| 指标 | 数值 |
|---|---|
| 通过断言 | 205 |
| 失败断言 | 9 |
| 崩溃用例 | 1（`test_temp_pool`，SIGSEGV / exit 139） |
| 用例整体状态 | 7 个全绿、4 个红 |
| 端到端结果 | 服务进程存活但不处理任何请求（0 字节响应，5s 超时） |

**判定：7 个模块（内存池、读写锁、哈希表、三个队列、Response 构造、定时器、路由表）质量合格；缺陷集中在“监听 fd 交付 → 请求完整性判定 → epoll 关注位 → 查询串解析”这条主链路上。**

---

## 2. 测试范围与方法

### 2.1 被测模块与调用链

```
main / WebStater
  └── DriveListen(监听 fd)  ── BUG-01
        └── Receiver_Thread (epoll 事件循环) ── BUG-03
              └── Request_Temp_Pool (暂存区/半包粘包)
                    └── RequestIntergrityJudge (方法、长度、完整性) ── BUG-02, BUG-04
                          └── Request_Analysiser (Url/Query/Headers/Body) ── BUG-05
                                └── Router → Handler → Response → 发送(fd/偏移) / 定时器超时关闭
```

### 2.2 测试方法

1. **单元/组件级（主手段）**：每个用例独立可执行，只链接被测模块（`gcc -g -O0 -Wall`），覆盖正常路径、边界（半包/粘包/2KB→4KB 扩容/8KB URL/16KB 头/1MB 请求体上限）、异常路径（400/408/411/413/414/431/503）。
2. **崩溃隔离**：`test_integrity` 用 `fork()` 在子进程里调用被测函数，父进程用 `waitpid` 读取信号/退出码，使“必崩函数”不会带走整个用例，并能量化报告“被信号 11 终止”。
3. **端到端**：编译原版服务并在 `127.0.0.1:5343` 上启动，用 `curl -m 5` 发真实请求，同时用 `ss` 观察监听队列与套接字收发队列。
4. **崩溃定位**：`gdb -batch -ex run -ex bt` 取回溯与实参。
5. **构建集成**：把仓库整体复制到 `/tmp` 后执行根目录 `make`，验证与 `test/` 的相互影响（避免污染被测仓库）。

### 2.3 执行命令

```bash
# 单元/组件级测试（11 个用例）
make -C test run

# 编译原版服务（不修改根 Makefile、不覆盖仓库里的 main）
make TARGET=/tmp/cweb_orig \
     SRCS="$(find . -name '*.c' -not -path './test/*' -printf '%P\n')"

# 端到端探活
/tmp/cweb_orig &                       # 监听 127.0.0.1:5343
curl -sS -m 5 -o /dev/null -w 'code=%{http_code} size=%{size_download} time=%{time_total}\n' \
     http://127.0.0.1:5343/
ss -ltn | grep 5343                    # 观察 Recv-Q（未 accept 的连接数）

# 崩溃定位
gdb -batch -ex run -ex bt --args test/build/test_temp_pool
```

---

## 3. 模块级测试结果矩阵

| # | 用例 | 覆盖内容 | PASS | FAIL | 状态 |
|---|---|---|---|---|---|
| 1 | `test_memory_pool` | 分级分配、归还复用、池满 `notfull=0` 降级、超 64KB 走 malloc 兜底、非法释放拒绝 | 27 | 0 | ✅ 全绿 |
| 2 | `test_rwlock` | 并发读共享、写互斥、写优先（排队写者先于后来读者） | 12 | 0 | ✅ 全绿 |
| 3 | `test_hash` | RouterHash 插入/查找/扩容后节点迁移/重复注册拒绝、AnalysiserHash、`RequestGetter` | 30 | 0 | ✅ 全绿 |
| 4 | `test_queues` | WorkerQueue/TaskQueue/ReceiverQueue：FIFO、环形回绕、空取、满队列背压、pop 阻塞唤醒 | 36 | 0 | ✅ 全绿 |
| 5 | `test_parser` | Method/Url/Query/Headers/Body 解析、414/431/400 | 22 | **3** | ❌ BUG-05 |
| 6 | `test_response` | 头拼接格式、头缓冲不足降级 503、256KB 响应、文件响应 fd/offset | 18 | 0 | ✅ 全绿 |
| 7 | `test_temp_pool` | 半包/粘包（管道化）、2KB→4KB 扩容、400/411/413、POST 体末端覆盖 | — | — | ❌ **SIGSEGV（exit 139）BUG-02**，断言 0 条即崩 |
| 8 | `test_timer` | 小顶堆堆序、容量上限、重置/移除、408 超时关闭 | 36 | 0 | ✅ 全绿 |
| 9 | `test_router` | URL→Handler 精确匹配、未注册→404、前缀不匹配、重复注册 | 15 | 0 | ✅ 全绿 |
| 10 | `test_integrity` | 方法识别（fork 隔离）、`Content-Length` 数值、Url/Query 拆分 | 1 | **4** | ❌ BUG-02/04/05 |
| 11 | `test_epoll` | `EpollReadMount/ChangeRead/ChangeReAndWr/Delete`、缓冲满→腾空后的可写唤醒 | 8 | **2** | ❌ BUG-03 |
| | **合计** | | **205** | **9** | 7 绿 / 4 红 |

---

## 4. 端到端测试结果

被测对象：原版代码编译出的服务进程（`/tmp/cweb_orig`），监听 `127.0.0.1:5343`。

| # | 检查项 | 预期 | 实测 | 结论 |
|---|---|---|---|---|
| 1 | 服务进程启动 | 常驻监听 | 进程存活（pid 72149），未崩溃、无 stderr 输出 | ✅ |
| 2 | 监听套接字 | LISTEN 127.0.0.1:5343 | `LISTEN 0 100 127.0.0.1:5343` | ✅ |
| 3 | `GET /` 响应 | 200 + 响应体 | `curl: (28) Operation timed out after 5003 milliseconds with 0 bytes received`，`code=000 size=0 time=5.003686` | ❌ **BUG-01** |
| 4 | 连接是否被服务端接收 | 被 accept 并读取请求 | 请求期间监听套接字 `Recv-Q=1`（已有 1 个连接建立完成但从未 accept）；服务端套接字 `Recv-Q=78`（78 字节请求原文躺在接收缓冲区，从未被读取） | ❌ **BUG-01** |
| 5 | 服务是否崩溃 | 不崩溃 | 未崩溃（因为代码根本没走到请求处理，BUG-02 被 BUG-01 掩盖） | — |

> 说明：**BUG-01 使服务处于“TCP 层可达、HTTP 层完全不可用”的状态**。客户端表现为超时/无响应，而不是连接被拒；任何依赖“服务器返回响应”的端到端用例都无法在其之上建立。

### 4.1 被 BUG-01 掩盖的崩溃（BUG-02）在组件级的定位

`test_temp_pool` 触发真实调用链 `RequestIntergrityJudge → MethodMatch`，结果：

```
$ ./build/test_temp_pool ; echo $?
Segmentation fault (core dumped)
139

$ gdb -batch -ex run -ex bt --args ./build/test_temp_pool
Program received signal SIGSEGV, Segmentation fault.
0x000055555555766c in MethodMatch (hs=0x55555557ce90, sit=0x7fffffffdb70, method=0x0)
    at ../RequestCome/RequestIntergrity/RequestIntergrity.c:28
28          *method=strnstr_match(hs->begin,Method_p[*sit],8);
#0  MethodMatch (...) at RequestIntergrity.c:28
#1  RequestIntergrityJudge (hs=…, ptr=…, fail=…, fail_reason=…)
    at RequestIntergrity.c:78
#2  main () at src/test_temp_pool.c:51
```

**判定**：`method` 形参在函数首行被置为 `NULL`（`RequestIntergrity.c:24`），随后在 `:28` 通过该空指针写入 → 一旦有请求进入处理流程即 100% 崩溃。

---

## 5. 缺陷详情

### BUG-01（P0 阻断）监听 fd 未回传，服务不处理任何请求

| 项目 | 内容 |
|---|---|
| 位置 | `StaterMain/StarterMain.c:41-58`（`DriveListen`，`return 1` 前无 `*Fd = fd;`） |
| 现象 | 服务进程存活、端口 LISTEN 正常，但所有 HTTP 请求 5s 超时、0 字节响应 |
| 根因 | `int DriveListen(int* Fd, char* IPaddr, int PORT)` 创建、绑定、listen 了 `fd`，但**从未把 `fd` 写入出参 `*Fd`**。调用方以 `-1`（未初始化）作为监听 fd 交给接收线程，`epoll_ctl(ADD, -1)` 必然失败，接收线程永远等不到事件，连接被内核完成握手后堆在 accept 队列里没人处理 |
| 证据 | `ss -ltn` 在请求期间由 `LISTEN 0 100 …:5343` 变为 `LISTEN 1 100 …:5343`（`Recv-Q=1`：已有 1 个连接建立完成但从未 accept）；`ss -tn` 显示 `78 0 127.0.0.1:5343 127.0.0.1:49044`（78 字节请求原文躺在接收缓冲区未被读取） |
| 影响 | **服务完全不可用**（对外表现为“连上但没反应”），所有上层逻辑（解析、路由、响应）都被掩盖 |
| 建议修复 | `DriveListen` 返回前加 `*Fd = fd;` |
| 验证用例 | 端到端 `GET /` 应返回 200；`ss` 的 `Recv-Q` 应归零 |

### BUG-02（P0 阻断）`MethodMatch` 置空形参再解引用，首个请求必崩

| 项目 | 内容 |
|---|---|
| 位置 | `RequestCome/RequestIntergrity/RequestIntergrity.c:24`（`method=NULL;`）与 `:28`（`*method = strnstr_match(...)`） |
| 现象 | 一旦请求进入完整性判定，进程 SIGSEGV（`exit 139`） |
| 根因 | 想“初始化调用方指针”，却写成了修改形参本身 `method=NULL`；形参被置空后 `:28` 用 `*method` 写入 → 空指针解引用。注意 `:29 if(*method!=NULL)` 的判断也随之永久为假 |
| 证据 | `gdb` 回溯（见 4.1）：`MethodMatch (…, method=0x0) at RequestIntergrity.c:28`，调用者 `RequestIntergrityJudge (…:78)`；`test_integrity` 报“被信号 11（Segmentation fault）终止” |
| 影响 | **修好 BUG-01 后服务会变成“一有请求就崩”**；GET 时 `h_method` 还会因 `*method` 恒为 NULL 而反复重跑匹配循环 |
| 建议修复 | 改为 `*method = NULL;` |
| 验证用例 | `test_temp_pool` 应完整跑完且无崩溃（当前是 0 条断言即 SIGSEGV）、`test_integrity` 的“方法识别”子用例 |

### BUG-03（P0 阻断）`EPOLLIN || EPOLLOUT` 使写事件永不触发，大响应/大文件中途挂起

| 项目 | 内容 |
|---|---|
| 位置 | `FdConnectManage/EpollOperate/EpollOperate.c:30`（`ev.events = EPOLLIN || EPOLLOUT;`，配合 `:32` 的 `EPOLL_CTL_MOD`） |
| 现象 | 响应体较大（超过一次可写完的量，例如 10MB 文件下载）时，数据发送到 socket 缓冲写满后**永久停滞**，连接既不关闭也不再发数据 |
| 根因 | 用的是**逻辑或**而非**按位或**：`EPOLLIN || EPOLLOUT` 求值为 `1`（即 `EPOLLIN`），写关注位被彻底丢掉。框架在“一次 sendfile/写未写完”后调用 `EpollChangeReAndWr` 重新挂载等待下一次可写，但内核永远不会因为可写而上报事件 |
| 证据 | ① 编译器在编译该文件时直接告警：`EpollOperate.c:30:26: warning: enum constant in boolean context [-Wint-in-bool-context]`；② `test_epoll` 运行输出：`[info] 表达式 EPOLLIN \|\| EPOLLOUT = 1，而 EPOLLIN \| EPOLLOUT = 5`；`EpollChangeReAndWr 后 epoll_wait 返回 0 个事件`（超时 300ms）→ 2 条断言 FAIL；同理“缓冲腾空后仍为 0 个事件”。对照项“只挂 EPOLLIN 时不上报可写”PASS，排除测试本身问题 |
| 影响 | 大响应不可用；**这是“10MB 下不完整/卡住”的直接根因**。小响应因一次写完而长期掩盖该缺陷 |
| 建议修复 | 改为 `ev.events = EPOLLIN \| EPOLLOUT;` |
| 验证用例 | `test_epoll` 全绿；修复后必须补做 10MB 端到端下载回归（见第 6 节） |

### BUG-04（P1 功能）`Content-Length` 被重复偏移，长度恒解析为请求体内容

| 项目 | 内容 |
|---|---|
| 位置 | `RequestIntergrity.c:41`（`bodylength = bodylength + 15;`）；调用方已在 `:107` 做过 `body_length = body_length + 15;` |
| 现象 | 长度值解析错误（实测解析为 `0`）；413（请求体过大）永不触发；带请求体的请求拷贝长度错误 |
| 根因 | 指针被**连续跳过两次** `"Content-Length:"`（共 30 字节），实际从请求体中间开始读数字，`atoi` 得到 0 或请求体字节 |
| 证据 | `test_integrity`：`小请求体：期望 5，实际解析为 0` / `超大请求体：期望 2000000，实际解析为 0`；`test_temp_pool` 的 413 用例（`Content-Length: 2000000`）无法通过 |
| 影响 | 1MB 请求体上限形同虚设（大 body 会被接受）、`:113 target = target + 4 + len` 拷贝末端错位 → POST 体被截断/污染 |
| 建议修复 | 删除 `:41` 的额外 `+15`，函数内只 `while(*bodylength==' ') bodylength++;` |
| 验证用例 | `test_integrity` 的 2 条长度用例、`test_temp_pool` 的 413 用例 |

### BUG-05（P1 功能）查询串解析：`query_ing` 被弃用 + 首次 `strtok_r` 传 NULL → 参数全部丢失

| 项目 | 内容 |
|---|---|
| 位置 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:99`（算出的 `query_ing` 被弃用，编译告警 `unused variable 'query_ing'`）与 `:111-125`（`char *save2 = "";`、`char* line2 = "";`、`do { … line2 = strtok_r(NULL, "&", &save2); } while (line2 != NULL);`） |
| 现象 | URL 中带 `?` 时，查询参数解析不到（`Url` 拆分本身正确） |
| 根因 | `strtok_r` 的规矩：**第一次**调用必须把待切分字符串作为第 1 个参数传入，它才会用 `saveptr` 记住“下次从哪继续”；之后再传 `NULL`。本处：① 第 99 行已经把首个参数所在串算好（`/search?q=cline&page=2` → `query_ing = "q=cline&page=2"`），却**从未把它交给 `strtok_r`**（这就是那条 unused 告警）；② 第 111 行把 `save2` 初始化成空字符串 `""`，第 123 行首次就以 `NULL` 调用 → 函数走 `s = *save_ptr`（即 `""`）从空串开始切 → 立刻返回 `NULL`；③ 于是 `do/while` 只对 `line2 = ""` 空跑一次（`strchr("", '=')` 为 NULL，不插入任何键值）便退出，**一个参数都没进 `h->Query`**，且结构上 `/search?` 后的第一个参数永远不可能被处理。注意：本处不是“未初始化导致的随机行为”，而是**确定性地解析出 0 个参数**（若把 111 行改写成真正的 `char *save2;`，才会变成未定义行为/随机结果） |
| 证据 | ① **独立复现**（`/tmp/demo_query2.c`，照抄该函数结构编译运行）：现状代码输出 `结果: 解析到 0 个 query 参数`；仅把首次切分改为 `line2 = strtok_r(query_ing, "&", &save2)` 后输出 `解析到: [q] = [cline]`、`解析到: [page] = [2]`、`结果: 解析到 2 个 query 参数`；② `test_parser` 3 条 FAIL：`Query 参数 q=cline`、`Query 参数 page=2`、`有效查询参数 x=1 被解析`（对应 `test_parser.c:55/57/136`）；③ `test_integrity` 直接调用该函数：`Url=/search，查询参数 q=cline → 未命中`；④ 编译警告 `RequestAnalysiser.c:99: unused variable 'query_ing'`（本应作为首次切分的输入） |
| 影响 | 所有依赖 query 的业务（搜索、分页、回显）拿到空参数；`?a=1&b=2` 全部丢失 |
| 建议修复 | 首次 `char* line2 = strtok_r(query_ing, "&", &save2);` 再用 `while (line2 != NULL) { …; line2 = strtok_r(NULL, "&", &save2); }` |
| 验证用例 | `test_parser`（25 条全过）、`test_integrity` 的 Url/Query 子用例 |

### BUG-06（P2 次要）`StartThread` 非 void 函数缺少返回值

| 项目 | 内容 |
|---|---|
| 位置 | `StaterMain/StarterMain.c:60-82`（`:82` 函数结束处无 `return`） |
| 现象 | 编译警告 `control reaches end of non-void function [-Wreturn-type]` |
| 根因 | 函数声明为 `int`，最后一行是 `ReveiverThreadMain(...)` 且其返回值未 `return` |
| 影响 | 返回值是不可预期的寄存器垃圾值；当前调用方未使用该返回值，故功能上暂未暴露，但属隐式 UB，重构/告警升级后会成为隐患 |
| 建议修复 | 补 `return ReveiverThreadMain(...);` 或改为 `void` 并在调用处适配 |

### BUG-07（P2 次要）示例 handler 类型错误且未注册任何路由

| 项目 | 内容 |
|---|---|
| 位置 | `main.c:4`（`printf("%c","hello world");`）、`main.c:12`（`WebStater("127.0.0.1",5343,asd)`） |
| 现象 | 编译警告 `format '%c' expects argument of type 'int', but argument 2 has type 'char *'` |
| 根因 | `%c` 接收 int，实际传入 `char*` → 未定义行为（多数实现打印首地址低位字节） |
| 影响 | 该函数是 `WebStater` 的唯一 handler；同时**项目未注册任何 URL 路由**，因此无法用于任何端到端 HTTP 功能验证 |
| 建议修复 | 改 `printf("%s", "hello world");`，并通过 `Router` 注册真实路由后再做端到端回归 |

### BUG-08（P2 次要）内存分配结果未检查

| 项目 | 内容 |
|---|---|
| 位置 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:19`（`int e0=MemoryPoolAlloc(...)`） |
| 现象 | 编译警告 `unused variable 'e0'` |
| 根因 | 分配返回值被丢弃，后续直接使用分配到的缓冲区 |
| 影响 | 内存池耗尽（`notfull=0`）时不会走错误分支，将使用空指针/未初始化区域；属潜在崩溃点 |
| 建议修复 | 检查 `e0`，失败时返回 503/错误码 |

### BUG-09（P2 集成）根 Makefile 通配把测试源码编入主程序

| 项目 | 内容 |
|---|---|
| 位置 | 根 `Makefile:17`（`SRCS = $(wildcard *.c */*.c */*/*.c */*/*/*.c)`） |
| 现象 | 只要 `test/` 存在，根目录执行 `make` 即报错 |
| 根因 | `test/src/test_x.c` 匹配 `*/*/*.c` 被纳入 `SRCS`，而每个用例都定义了 `main`（另有 `test_router.c` 与 `GlobalResource.o` 的 `router` 重复定义） |
| 证据 | `/tmp` 副本执行 `make`（原始输出见附录 C） |
| 影响 | 测试代码无法与主程序共存于同一套构建；CI/本地全量构建会失败 |
| 建议修复 | `Makefile` 增加 `SRCS := $(filter-out test/%,$(SRCS))`；或测试产物改用独立目录/独立目标 |
| 临时绕开（无需改你的文件） | `make TARGET=/tmp/cweb_orig SRCS="$(find . -name '*.c' -not -path './test/*' -printf '%P\n')"` |

### BUG-10（P3 提示）`-Wall` 下的静态告警清单（部分可直接指向缺陷）

| 告警位置 | 告警内容 | 关联 |
|---|---|---|
| `FdConnectManage/EpollOperate/EpollOperate.c:30:26` | `enum constant in boolean context [-Wint-in-bool-context]` | **BUG-03 的编译器证据**（编译器已指出把枚举当布尔用） |
| `RequestCome/RequestAnalysis/RequestAnalysiser.c:99:15` | `unused variable 'query_ing'` | **BUG-05 的编译器证据**（本应作为首次 `strtok_r` 的输入） |
| `RequestCome/RequestAnalysis/RequestAnalysiser.c:19:9` | `unused variable 'e0'` | BUG-08 |
| `MemoryPool/MemoryPool.c:42:1` | `control reaches end of non-void function [-Wreturn-type]` | 与 BUG-06 同类（`MemoryStackExpend` 缺 `return`） |
| `MyLock/my_rwlock_t.c:23:1` / `:36:1` / `:51:1` | `control reaches end of non-void function [-Wreturn-type]` | 与 BUG-06 同类（`my_lock_rdlock` / `my_lock_wrlock` 等缺 `return`；测试中锁行为正常，但返回值不可依赖） |
| `RequestAndResponse/Response.c:8:9` | `unused variable 'n'` | 冗余代码 |
| `FdConnectManage/Timer/Timer.c:87:9` | `variable 'fd' set but not used [-Wunused-but-set-variable]` | 疑似逻辑遗漏，建议复核超时关闭路径 |
| `main.c:4`（编译主程序时） | `format '%c' expects argument of type 'int', but argument 2 has type 'char *'` | BUG-07 |
| `StaterMain/StarterMain.c:82`（编译主程序时） | `control reaches end of non-void function` | BUG-06 |

**建议**：把 `-Wall -Wextra` 纳入构建并在 CI 中对 `-Wint-in-bool-context`、`-Wreturn-type`、`-Wformat` 采取零容忍（本次 9 个缺陷中有 4 个可由编译器提前发现）。

---

## 6. 未能端到端验证的部分与风险说明

1. **BUG-03 的端到端表现（本次无法复现）**：原始 `main.c` 未注册任何路由，10MB 测试资源 `WebSite/static/10mb.bin` 也已按约定删除，因此在当前仓库状态下无法构造“真实 10MB 文件下载”场景。BUG-03 结论依据为：① `test_epoll` 在 epoll 事件层的直接证据（写关注位丢失、缓冲腾空后仍无事件）；② 历史观测（此前在“仅修复 BUG-01/BUG-02”的临时副本上，10MB 下载稳定停在同一位置 **2703360 字节 ≈ 2.58MB**，即首批内核发送缓冲耗尽处）。**该停点数据标记为历史观测，需在 BUG-03 修复后补做端到端回归确认。**
2. **BUG-04 对 POST 体截断的影响**：`test_temp_pool` 的 POST 用例因 BUG-02 提前崩溃而未执行到，本次仅有长度解析层面的直接证据与代码路径推断，建议修复 BUG-02 后重跑确认。
3. **BUG-01 掩盖效应**：在 BUG-01 存在的情况下，任何端到端 HTTP 断言都只能得到“无响应”，无法观测上游缺陷；因此本次采用“组件级直接验证 + fork 隔离”来分离缺陷，避免被掩盖。
4. 未覆盖范围：chunked 传输、HTTP/2、WebSocket、`Range`/`HEAD`、真实高并发压测（连接数/长时间稳定性）均不在本次范围内。

---

## 7. 测试资产与工作区状态

| 类别 | 内容 |
|---|---|
| 新增（仅 `test/` 下） | `test/Makefile`、`test/run_tests.sh`、`test/website_test.sh`、`test/TEST_REPORT.md`、`test/src/test_*.c` ×11（`memory_pool`/`rwlock`/`hash`/`queues`/`parser`/`response`/`temp_pool`/`timer`/`router`/`integrity`/`epoll`）、`test/build/` 编译产物 |
| 本次报告新增用例 | `test/src/test_integrity.c`（方法识别 / Content-Length / Url+Query，`fork` 隔离崩溃）、`test/src/test_epoll.c`（读写关注位与可写唤醒） |
| 已删除（原本由本人创建） | `WebSite/`（`WebSite.c/.h`、`static/10mb.bin`）—— 按约定只保留 `test/` |
| 你的文件 | **零改动**：`git status` 中无任何已跟踪文件被修改；`main`、`main.c`、根 `Makefile`、`README.md`、`.vscode/tasks.json`、5 个源文件均与 `314174f` 一致 |
| 其他清理 | 删除本人编译产生的 28 个 `*.o`；无 core dump 残留；无遗留服务进程 |
| 构建产物（仓库外） | `/tmp/cweb_orig`（原版服务二进制）、`/tmp/cweb_mk`（用于构建冲突验证的副本） |
| 原始日志 | `/tmp/unit_orig2.log`（11 用例全量输出）、`/tmp/e2e_orig.log`（端到端）、`/tmp/tp_gdb.log`（gdb 回溯）、`/tmp/mk_proof.log`（构建冲突） |
| 注意事项 | `test/website_test.sh` 目前为“孤儿脚本”：其校验的 `/`、`/download/10mb` 等路由随 `WebSite/` 一并删除，在原版 `main.c` 下无对应路由可测；如需恢复 10MB 端到端测试，需自行注册路由并提供 10MB 静态文件 |

---

## 8. 修复优先级与回归验收标准

| 顺序 | 缺陷 | 修复动作（一行级） | 验收标准 |
|---|---|---|---|
| 1 | BUG-01 | `StarterMain.c` `DriveListen` 内加 `*Fd = fd;` | 端到端 `GET /` 得到响应（不再是 000）；请求期间 `ss` 的 `Recv-Q` 归零 |
| 2 | BUG-02 | `RequestIntergrity.c:24` 改为 `*method = NULL;` | `test_temp_pool` 完整跑完且无 SIGSEGV；`test_integrity` 方法识别 PASS |
| 3 | BUG-03 | `EpollOperate.c:30` 改为 `EPOLLIN \| EPOLLOUT` | `test_epoll` 10/10；**补做 10MB 端到端下载**：收到字节数 == 文件大小、md5 一致、连接正常关闭 |
| 4 | BUG-04 | 删除 `RequestIntergrity.c:41` 的 `+15` | `test_integrity` 长度 2 条 PASS；`test_temp_pool` 413 用例 PASS |
| 5 | BUG-05 | 重写 `RequestAnalysiser.c:111-125` 的 `strtok_r` 循环（用 `query_ing` 首次切分） | `test_parser` 25/25；`test_integrity` Url/Query 用例 PASS |
| 6 | BUG-06 / BUG-10 | 补 `StartThread`、`MemoryStackExpend`、`my_lock_rdlock/wrlock` 的 `return`；清理无用变量 `e0`/`n`/`fd` | `-Wall` 编译零告警 |
| 7 | BUG-09 | 根 `Makefile:17` 后加 `SRCS := $(filter-out test/%,$(SRCS))` | 仓库根目录 `make` 成功；`make -C test run` 仍可用 |
| 8 | BUG-07 / BUG-08 | 注册真实路由、修正 `printf("%s")`、检查分配返回值 | 多路由端到端回归（GET/POST/404/413/408）通过 |

**整体验收门槛**

1. `make -C test run` → **11/11 用例通过、0 FAIL、无崩溃**（当前 205 PASS / 9 FAIL / 1 崩溃；修复后至少 214 条断言全绿，`test_temp_pool` 条数以实际运行为准）。
2. 端到端：`GET`/`POST` 正常、可选路由 404、超长 URL 414、超大 body 413、断连 408 释放、**10MB 文件完整下载**。
3. `-Wall`（建议再加 `-Wextra`）无告警。

---

## 9. 修改后复测（第二版：被测代码已被修改）

> 本章记录针对**使用者修改后代码**（工作区 4 个文件已改动、尚未提交）的复测结论。第 1~8 章针对 `314174f` 原版基线，保留原样不动。

### 9.1 复测对象与指纹

| 文件 | 修改时间 | SHA-256（前 16 位） | 改动摘要 |
|---|---|---|---|
| `StaterMain/StarterMain.c` | 22:42:58 | `a54524f022ca4511` | `DriveListen` 内 `int fd = socket(...)` → `*Fd = socket(...)`（BUG-01）；`StartThread` 末尾补 `return 1`（BUG-06） |
| `FdConnectManage/EpollOperate/EpollOperate.c` | 22:39:16 | `4b572fce63e29a0c` | `EPOLLIN \|\| EPOLLOUT` → `EPOLLIN \| EPOLLOUT`（BUG-03） |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | 22:40:35 | `b64a8886cf4dd2ea` | 删除 `MethodMatch` 内的 `method=NULL;`（BUG-02）；`+15` 由调用方移入 `BodyLength` 内部，保证只跳一次（BUG-04） |
| `RequestCome/RequestAnalysis/RequestAnalysiser.c` | 22:46:13 | `f494a5835fb45ab2` | 查询串循环改写（`save2=""`；`strtok_r(query_ing,...)` 挪进循环体首行；删除循环尾部的 `strtok_r(NULL,...)`）→ **结果见 9.3** |

HEAD 仍为 `314174f`（`314174f 编译通过`），以上 4 个文件是**未提交的工作区改动**，其余文件与 `314174f` 一致。

### 9.2 复测结果（每个用例 `timeout -k 5 25` 兜底，完整日志 `/tmp/unit_final.log`）

| # | 用例 | 结果 | 退出码 | 说明 |
|---|---|---|---|---|
| 1 | `test_memory_pool` | PASS 27 / FAIL 0 | 0 | 无变化 |
| 2 | `test_rwlock` | PASS 12 / FAIL 0 | 0 | 无变化 |
| 3 | `test_hash` | PASS 30 / FAIL 0 | 0 | 无变化 |
| 4 | `test_queues` | PASS 36 / FAIL 0 | 0 | 无变化 |
| 5 | `test_parser` | **卡死（HANG）** | 124 | 被超时强制终止；唯一输出是 `[PASS] 初始化业务线程内存池`，随后卡在**第一个解析用例** `GET /search?q=cline&page=2`（`test_parser.c:46`）（BUG-05R） |
| 6 | `test_response` | PASS 18 / FAIL 0 | 0 | 无变化 |
| 7 | `test_temp_pool` | PASS 40 / FAIL 0 | 0 | **BUG-02 / BUG-04 修复确认**（原为 0 条断言即 SIGSEGV） |
| 8 | `test_timer` | PASS 36 / FAIL 0 | 0 | 无变化 |
| 9 | `test_router` | PASS 15 / FAIL 0 | 0 | 无变化 |
| 10 | `test_integrity` | **卡死（HANG）** | **124** | 先 4 条 PASS，卡在第 5 条“`/search?q=cline&page=2`”（BUG-05R） |
| 11 | `test_epoll` | PASS 10 / FAIL 0 | 0 | **BUG-03 修复确认**（原为 8 PASS / 2 FAIL） |

| 指标 | 第二版（修复后） | 第一版（原版 `314174f`） |
|---|---|---|
| 通过断言 | **229** | 205 |
| 失败断言 | **0** | 9 |
| 崩溃用例 | **0** | 1（`test_temp_pool` SIGSEGV） |
| **卡死用例** | **2**（`test_parser`、`test_integrity`） | 0 |
| 全绿用例 | 9 / 11 | 7 / 11 |
| 端到端 | `GET /` → **HTTP 404 / 0.05s**（已能回包）；`GET /?a=1` → **5s 无响应（curl 28 / HTTP 000）** | 任何请求 5s 无响应（HTTP 000） |

### 9.3 新引入的回归：BUG-05R（P0 阻断）查询串切分死循环 + 空查询串崩溃

| 项目 | 内容 |
|---|---|
| 位置 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:111-125` |
| 现象 | ① URL 带 `?k=v` 时函数**永不返回**，所在线程 100% CPU 空转；② URL 只有 `?`（空查询串）时 **SIGSEGV** |
| 当前代码 | `char *save2=""; char* line2=""; do{ line2 = strtok_r(query_ing,"&",&save2); char* m=strchr(line2,'='); … }while(line2!=NULL);` |
| 根因 | `strtok_r` 第 1 个参数**非 NULL** 的语义是“从这个串首重新开始切分”。这里**每一轮都传 `query_ing`**，于是永远只切出第一个 token：首轮 `strtok_r` 返回 `q=cline`，第 118 行 `*m='\0'` 把缓冲区改成 `q\0cline&page=2`；此后每轮只切出 `"q"`（没有 `=`、不入表、也不推进 `save2`），而 `line2` 恒非 NULL → `while` 条件恒真 → **确定性死循环**。真正负责推进遍历的 `strtok_r(NULL,"&",&save2)` 已在本次改动中被删除，循环体内再没有任何语句能改变循环条件 |
| 证据 | 见下方 5 条（源码 diff、最小复现、用例卡死定位、套件结果、端到端） |
| 影响 | **对外 DoS**：任何携带查询串的请求（`/?a=1`、`/search?q=x`，甚至 `/?` 触发崩溃）都会把接收线程永久占死为 100% CPU，该线程不再处理新连接、连接与内存不释放。属可用性致命问题 |
| 建议修复 | 见 **9.6**（一次性初始化 + `while` 推进的写法） |
| 验收用例 | `test_parser`（25 条全过不卡死）、`test_integrity`（Url/Query 子用例）、端到端 `GET /?a=1` 必须正常回包 |

**证据 1（源码 diff，`/tmp/diff_final.txt`）**

```diff
         //开始切查询条件
-        char *save2;
+        char *save2 = "";
         char* line2 = "";
         do{ 
         // 处理 line2，按 = 切键和值
+            line2 = strtok_r(query_ing, "&", &save2);
             char* m=strchr(line2,'=');
             if(m){
                 *m='\0';
@@
                 if(e1!=1){  *failReason=400;  return -1;  }
             }
                     
-            line2 = strtok_r(NULL, "&", &save2);
 
             }while (line2 != NULL);
```

**证据 2（最小复现 `/tmp/demo_query3.c`，照抄该循环结构去掉哈希表）**

```
--- 模式1：?q=cline&page=2 （正常业务请求形状）---
url=[]  query_ing=[q=cline&page=2]
  第 0轮：插入键=[q] 值=[cline]
  第 1轮：line2=[q] 无 '=' 不入表
  第 2轮：line2=[q] 无 '=' 不入表
  …（第 3~15 轮完全相同）
  …… 已循环 16 轮，条件 line2!=NULL 永远成立 → 死循环

--- 模式2：? （只有问号、空查询串）---
段错误（核心已转储）        退出码=139（SIGSEGV）
```

**证据 3（用例卡死前的进度，行缓冲定位）** `cd test && timeout 10 stdbuf -o0 -e0 ./build/test_parser`

```
========== test_parser ==========
== test_parser : HTTP 请求头/URL/Query/Body 解析 ==
  [PASS] 初始化业务线程内存池（1KB~64KB 类）          ← 这是它唯一的输出
   [HANG] test_parser 超过 30s 未返回，已强制终止 → 被测函数很可能死循环
```

`test_parser.c:37` 之后紧跟的第一条用例就是第 46 行的 `do_parse("GET /search?q=cline&page=2 …")`，说明卡点正是**请求行 URL/查询串解析**（`DealUrlAndQuery`）——连“解析结构分配成功”这条断言都没能输出。

**证据 4（套件级原始输出，`/tmp/unit_final.log`）**

```
========== test_parser ==========
   >>> test_parser 退出码=124                  ← 25s 超时被杀，连缓冲里的断言都没来得及刷出
========== test_integrity ==========
  [PASS] 初始化解析用内存池
  [PASS] MethodMatch 能识别 GET（不应崩溃）          ← BUG-02 已修好
  [PASS] Content-Length: 5 → 解析出 5               ← BUG-04 已修好
  [PASS] Content-Length: 2000000 → 解析出 2000000   ← BUG-04 已修好
   >>> test_integrity 退出码=124               ← 卡在第 5 条 “/search?q=cline&page=2”
```

**证据 5（端到端，用当前代码编译的服务端，`/tmp/e2e_fixed_out2.txt`）**

```
--- A) GET /      ---  HTTP=404  time=0.050796s   ← BUG-01 已修好：连接被处理并正常回包
--- B) GET /?a=1  ---  HTTP=000  time=5.003145s   curl 退出码=28（5s 超时、0 字节）
--- C) 卡住后的 CPU ---  PID 80357  %CPU 78.7
                        LWP 80360  %CPU 79.6  STAT Rl   ← 单个接收线程 100% 空转（Running）
--- D) 收尾 ---         (无残留)
```

### 9.4 已确认修好的缺陷（第一版报告中的 5 项）

| 缺陷 | 复测证据 | 结论 |
|---|---|---|
| BUG-01 P0 监听 fd 未回传 | 端到端 `GET /` → `HTTP 404 / 0.05s`（原为 000 / 5s 超时）；返回体 `Not Found` 属路由未注册（BUG-07 未修），说明“收包 → 判定 → 解析 → 路由未命中 → 回 404”整条链路已经跑通 | ✅ 已修复 |
| BUG-02 P0 `method=NULL` | `test_temp_pool` 由“0 条断言即 SIGSEGV”变为 **40 PASS / 0 FAIL**；`test_integrity` 的 `MethodMatch 能识别 GET` PASS | ✅ 已修复 |
| BUG-03 P0 `EPOLLIN \|\| EPOLLOUT` | `test_epoll` **10 PASS / 0 FAIL**（写关注位、缓冲写满不上报、腾空后上报 EPOLLOUT、退回只读、删除 fd 均正确）；`-Wall` 中 `EpollOperate.c:30` 的 `-Wint-in-bool-context` 告警消失 | ✅ 已修复 |
| BUG-04 P1 `Content-Length` 双重 `+15` | `test_integrity` 长度 2 条 PASS（`5 → 5`、`2000000 → 2000000`）；`test_temp_pool` 的 `Content-Length > 1MB → 413`、`带 5 字节请求体的 POST 判定完整` 等用例全部 PASS | ✅ 已修复 |
| BUG-06 P2 `StartThread` 缺 return | `StartThread` 末尾补 `return 1`；`StaterMain/StarterMain.c:82` 的 `control reaches end of non-void function` 告警消失 | ✅ 已修复 |
| BUG-05 P1 查询串解析 | 由“解析出 0 个参数”变成“**死循环 + 空查询串崩溃**” | ❌ **未修复且恶化为 BUG-05R（P0）**，见 9.3 |

> 说明（测试侧同步调整）：使用者把 `+15` 移进 `BodyLength` 内部后，`BodyLength` 的调用契约变为“传行首指针”。我把 `test/src/test_integrity.c` 的 `body_length_check` 同步改为 `BodyLength(&len, buf)`（见该文件 64-71 行注释），否则旧测试会按“调用方已跳 15”的旧契约误判 BUG-04 仍失败。

### 9.5 第一版报告中仍未处理的项（复测后现状）

| 编号 | 现状 | 影响 |
|---|---|---|
| **BUG-05R** | 见 9.3 | **P0 阻断（本次新引入）**：带查询串的请求即 DoS，`/?` 直接崩溃 |
| BUG-07 | `main.c:4` 仍是 `printf("%c","hello world")`（`-Wall` 告警仍在），且**仍未注册任何路由** | 端到端只能看到 404；解析→路由→响应→发送的全链路（含 10MB 大文件）仍无法做业务级验证 |
| BUG-08 | `RequestAnalysiser.c:19` 的 `e0` 仍未检查（`unused variable 'e0'` 告警仍在） | 内存池耗尽时静默使用空指针 |
| BUG-09 | 根 `Makefile:17` 未加 `filter-out`：复测把仓库整体复制到 `/tmp/cweb_fixed` 后执行 `make`，**再次复现** `multiple definition of 'main'; main.o … first defined here` 与 `multiple definition of 'FdClose'`，链接失败（见 9.7 下方的构建录制） | `test/` 与主程序无法共存；根目录 `make` 失败。**1 行修复**：`SRCS := $(filter-out test/%,$(SRCS))`；临时绕开见 `test/Makefile` 头部注释 |
| BUG-10 | 干净全量重编后共 **18 条** `-Wall` 告警（第一版因增量编译只统计到 8 条，现补齐清单，见 9.7）；其中 3 条已随本次修复消失 | 见 9.7 |


### 9.6 BUG-05R 建议修复（可直接替换 `RequestAnalysiser.c:111-125`）

```c
        // 开始切查询条件：首次必须把待切串交给 strtok_r，之后才传 NULL
        char* save2 = NULL;
        char* line2 = strtok_r(query_ing, "&", &save2);
        while (line2 != NULL) {
            char* m = strchr(line2, '=');
            if (m) {
                *m = '\0';
                int e1 = AnalysiserHashInsert(h->Query, h, line2, m + 1);
                if (e1 != 1) { *failReason = 400; return -1; }
            }
            line2 = strtok_r(NULL, "&", &save2);   // ← 推进到下一个参数
        }
```

要点：

1. **首次**传 `query_ing`、**其余**传 `NULL`，`strtok_r` 的 `saveptr` 才会逐段推进；“推进语句放在循环体末尾”与“循环体首行用 `strtok_r(NULL,...)`”两种写法只能选一种，**绝不能每一轮都传非 NULL 的 `query_ing`**（那等于每轮都从串首重新开始）。
2. 用 `while (line2 != NULL)` 结构，`strchr` 只在 `line2` 非空时调用 → 顺带消除 `?`、`?&` 这类空查询串触发的 `strchr(NULL)` 崩溃。
3. `save2` 初值无关紧要（首次调用即被写入），不需要 `= ""`；而 `line2` 也不需要初值。
4. 建议顺手明确 `?a=`（空值）与重复键 `?a=1&a=2` 的策略——当前最终行为由 `AnalysiserHashInsert` 决定，测试用例尚未覆盖这两种输入。

### 9.7 复测后的 `-Wall` 全量告警清单（干净全量重编，`/tmp/warn_after.txt`）

```
main.c:4:14                                            format '%c' expects argument of type 'int', but argument 2 has type 'char *'   ← BUG-07
RequestCome/RequestAnalysis/RequestAnalysiser.c:19:9    unused variable 'e0'                                                   ← BUG-08
FdConnectOrClose/FdConnectOrClose.c:46:13               unused variable 'n'
GlobalResource/GlobalResource.c:15:1                    control reaches end of non-void function
MemoryPool/MemoryPool.c:42:1                            control reaches end of non-void function
MyLock/my_rwlock_t.c:23:1 / :36:1 / :51:1               control reaches end of non-void function
RequestAndResponse/Response.c:8:9                       unused variable 'n'
StaterMain/StarterMain.c:19:1                           control reaches end of non-void function（IgnoreSignal）
Threads/ReceiverThread.c:25:9 / :54:19                  unused variable 'e0'
Threads/ReceiverThread.c:40:21                          unused variable 'e2'
Threads/ReceiverThread.c:59:25                          unused variable 'e1'
Threads/ReceiverThread.c:26:1                           control reaches end of non-void function
Threads/WorkerThread.c:149:5                            control reaches end of non-void function
FdConnectManage/Timer/Timer.c:87:9                      variable 'fd' set but not used（疑似逻辑遗漏，建议复核超时关闭路径）
FdConnectResource/SendPointerPool/SendPointerPool.c:63:1  control reaches end of non-void function
```

- **修复后消失的 3 条**：`EpollOperate.c:30`（`enum constant in boolean context` ← BUG-03 的编译器证据）、`RequestAnalysiser.c:99`（`unused variable 'query_ing'` ← BUG-05 的表征）、`StaterMain/StarterMain.c:82`（`-Wreturn-type` ← BUG-06）。
- **本次新暴露**：`ReceiverThread.c` 的 4 条未使用变量告警（`e0`/`e0`/`e1`/`e2`）——预分配/登记/发送链路上的分配结果同样没有被检查，与 BUG-08 同类，建议一并复核。

同时附上 BUG-09 的构建复现（仓库整体拷贝到 `/tmp/cweb_fixed` 后执行根 `make`，含 `test/`）：

```
/usr/bin/x86_64-linux-gnu-ld.bfd: test/src/test_temp_pool.o: in function `main':
test/src/test_temp_pool.c:29: multiple definition of `main'; main.o:main.c:6: first defined here
/usr/bin/x86_64-linux-gnu-ld.bfd: test/src/test_timer.o: in function `FdClose':
test/src/test_timer.c:16: multiple definition of `FdClose'; FdConnectOrClose/FdConnectOrClose.o:41: first defined here
collect2: error: ld returned 1 exit status
make: *** [Makefile:22：main] 错误 1
```

（同一份代码**排除 `test/`** 后即可正常编译并运行——见 9.3 证据 5。）

### 9.8 复测原始日志索引

| 日志 | 内容 |
|---|---|
| `/tmp/unit_final.log` | 11 个用例的完整复测输出（含 2 个 `退出码=124` 卡死） |
| `/tmp/hang_point.txt` | 行缓冲重跑 `test_parser`，定位卡死位置 |
| `/tmp/demo_query3.c` / `/tmp/demo_query3_out.txt` | BUG-05R 最小复现源码与输出（死循环 + 空查询串 SIGSEGV） |
| `/tmp/e2e_fixed_out2.txt` | 当前代码端到端（`GET /` 404；`GET /?a=1` 超时；单线程 100% CPU） |
| `/tmp/diff_final.txt` | 4 个改动文件的完整 `git diff` |
| `/tmp/fingerprint.txt` | 复测版本指纹（HEAD、改动清单、sha256、mtime） |
| `/tmp/warn_after.txt` | 复测后 `-Wall` 全量告警清单 |

### 9.9 复测总结与放行判定

- **已修好（5 项，均有实测证据）**：BUG-01、BUG-02、BUG-03、BUG-04、BUG-06。套件指标从“205 PASS / 9 FAIL / 1 崩溃”变为“**229 PASS / 0 FAIL / 0 崩溃，另有 2 个用例 HANG**”。
- **未修好且升级（1 项）**：BUG-05 → **BUG-05R（P0）**。`?k=v` 使接收线程确定性死循环（100% CPU、永不返回），`?` 直接 SIGSEGV；端到端已复现 `GET /?a=1` 5s 无响应。
- **仍未处理（4 项）**：BUG-07（无路由 / UB handler）、BUG-08（分配结果未检查）、BUG-09（根 Makefile 通配冲突）、BUG-10（18 条 `-Wall` 告警）。
- **放行判定：仍不建议对外提供服务。** 只要 BUG-05R 存在，任何携带查询串的请求都能把一个服务线程永久锁死（可用性 P0）。
- **修完 BUG-05R 后的回归门槛**：重跑 `test_parser`、`test_integrity`、`test_temp_pool`、`test_epoll` 四个用例（必须 100% 通过且无 HANG），并复做端到端：`GET /`（期望 404）、`GET /?a=1`（期望正常回包、不超时）、10MB 下载（期望字节数与摘要一致）。

### 9.10 用改进后的 `run` 目标复跑（最终验证，日志 `/tmp/unit_run3.log`）

原先 `make -C test run` 遇到卡死用例会**永久挂住**（本次复测第一次就卡在这里，只能人工 kill）。现在 `test/Makefile` 的 `run` 目标改为 `timeout -k 5 30 stdbuf -o0 -e0 ./build/<用例>` + `[HANG]` 判定（`stdbuf` 让卡死前的进度也能落盘），复跑结果：

```
========== test_parser ==========
== test_parser : HTTP 请求头/URL/Query/Body 解析 ==
  [PASS] 初始化业务线程内存池（1KB~64KB 类）
   [HANG] test_parser 超过 30s 未返回，已强制终止 → 被测函数很可能死循环（上面最后一行是卡住前的进度）
========== test_response ==========
---- test_response: PASS 18 / FAIL 0 ----
…（hash/queues/temp_pool/timer/router 全绿）…
========== test_integrity ==========
  [PASS] 初始化解析用内存池
  [PASS] MethodMatch 能识别 GET（不应崩溃）
  [PASS] Content-Length: 5 → 解析出 5
  [PASS] Content-Length: 2000000 → 解析出 2000000（413 判定依赖它）
   [HANG] test_integrity 超过 30s 未返回，已强制终止 → 被测函数很可能死循环（上面最后一行是卡住前的进度）
========== test_epoll ==========
---- test_epoll: PASS 10 / FAIL 0 ----
==== 存在失败/卡死用例 ====
make: *** [Makefile:62：run] 错误 1
RUN_EXIT=2
```

| 指标 | 值 |
|---|---|
| PASS / FAIL | **229 / 0** |
| HANG | **2**（`test_parser`、`test_integrity`） |
| 全绿用例 | 9 / 11 |
| 套件退出 | `run` 目标 exit 1（`make` 记为 RUN_EXIT=2），不再无限等待 |

> 两次运行的断言数相差 1 条：第一次（25s 超时、无 `stdbuf`）`test_parser` 卡死时，缓冲区里那 1 条 PASS 随 SIGTERM 丢失，故记 228；9.2 表格与本节均以可复现的 **229** 为准。


> 复测方式说明：全程**只读**运行被测代码，**未改动那 4 个源文件**。本轮只修改了 `test/` 下我自己的测试资产：① `test/src/test_integrity.c` 的 `body_length_check` 改为按“`BodyLength` 内部 `+15`”的新契约调用（`BodyLength(&len, buf)`），与新修法一致；② `test/Makefile` 的 `run` 目标加 30s/用例 `timeout` + `[HANG]` 判定（避免卡死用例把整个套件永久挂住），并把注释里“根 Makefile 已 filter-out”的旧说法纠正为 BUG-09 的实际处理方式。

---

---

## 10. 第三次复测（2026-10-14，BUG-05R 修复后）

> 本章针对**用户第二轮修改**（相对第二版，源码只多了 `RequestCome/RequestAnalysis/RequestAnalysiser.c` 的增量改动）的复测结论。第 1~8 章为原版基线，第 9 章为第二版，均保留原样。

### 10.1 复测对象与指纹

| 文件 | 修改时间 | SHA-256（前 16 位） | 与第二版对比 |
|---|---|---|---|
| `RequestCome/RequestAnalysis/RequestAnalysiser.c` | 2026-10-14 13:56:56 | `1b6e3544edc34dd5` | **本轮唯一源码改动**：`line2 = strtok_r(query_ing, "&", &save2)` 移到 `do{}` 之前，循环尾恢复 `line2 = strtok_r(NULL, "&", &save2)` 推进（即 9.6 建议写法） |
| `StaterMain/StarterMain.c` | 2026-10-13 22:42:58 | `a54524f022ca4511` | 未再改（BUG-01 / BUG-06 修复保持） |
| `FdConnectManage/EpollOperate/EpollOperate.c` | 2026-10-13 22:39:16 | `4b572fce63e29a0c` | 未再改（BUG-03 修复保持） |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | 2026-10-13 22:40:35 | `b64a8886cf4dd2ea` | 未再改（BUG-02 / BUG-04 修复保持） |

工作区另有一个被重建的二进制 `main`（2026-10-14 13:53:37，178272 字节，`git status` 显示 `M main`），属构建产物，不是源码改动。

### 10.2 复测结果：11/11 用例全绿，无 FAIL、无 HANG

`cd test && make clean && make run`（日志 `/tmp/unit_run4.log`，`RUN_EXIT=0`，末行 `==== 全部单元测试通过 ====`）

| # | 用例 | 第三版 | 第二版 |
|---|---|---|---|
| 1 | `test_memory_pool` | PASS 27 / FAIL 0 | PASS 27 |
| 2 | `test_rwlock` | PASS 12 / FAIL 0 | PASS 12 |
| 3 | `test_hash` | PASS 30 / FAIL 0 | PASS 30 |
| 4 | `test_queues` | PASS 36 / FAIL 0 | PASS 36 |
| 5 | `test_parser` | **PASS 25 / FAIL 0** | **HANG**（卡在 `?q=cline&page=2`） |
| 6 | `test_response` | PASS 18 / FAIL 0 | PASS 18 |
| 7 | `test_temp_pool` | PASS 40 / FAIL 0 | PASS 40 |
| 8 | `test_timer` | PASS 36 / FAIL 0 | PASS 36 |
| 9 | `test_router` | PASS 15 / FAIL 0 | PASS 15 |
| 10 | `test_integrity` | **PASS 5 / FAIL 0** | **HANG**（卡在 `/search?q=…`） |
| 11 | `test_epoll` | PASS 10 / FAIL 0 | PASS 10 |

| 指标 | 第三版 | 第二版 | 第一版（原版 `314174f`） |
|---|---|---|---|
| 通过断言 | **254** | 229 | 205 |
| 失败断言 | **0** | 0 | 9 |
| 卡死用例 | **0** | 2 | 0 |
| 崩溃用例 | **0** | 0 | 1（`test_temp_pool`） |
| 全绿用例 | **11 / 11** | 9 / 11 | 7 / 11 |
| 套件退出 | **RUN_EXIT=0** | RUN_EXIT=2 | — |

### 10.3 BUG-05R ①「查询串死循环」已修复 ✅

**证据 1（源码 diff，`/tmp/diff_v3.txt`）**

```diff
-        char *save2 = "";
-        char* line2 = "";
+        char *save2 = "";
+        char* line2 = strtok_r(query_ing, "&", &save2);
         do{ 
-            char* m=strchr(line2,'=');
-            ...
-            line2 = strtok_r(NULL, "&", &save2);
-
+        char* m=strchr(line2,'=');
+        if(m){ … }
+        line2 = strtok_r(NULL, "&", &save2);
             }while (line2 != NULL);
```

**证据 2（单测）**：`test_parser` 从 0 条断言即卡死 → **PASS 25 / FAIL 0**；第 7、8 条断言 `Query 参数 q=cline`、`Query 参数 page=2` 均通过，说明查询串已能**正确切分并全部入表**（原版 BUG-05 的"参数全部丢失"也一并解决）。

**证据 3（端到端 `/tmp/e2e_v3_out.txt`、`/tmp/e2e_v3c_out.txt`）**

```
--- A) GET /                HTTP=404 time=0.051327s  server_alive=yes
--- B) GET /?a=1            HTTP=404 time=0.051603s  server_alive=yes   ← 第二版此请求 5s 无响应
--- C) GET /?q=cline&page=2 HTTP=404 time=0.050951s  server_alive=yes
--- 负载检查：PID 87561 STAT Sl+ %CPU 0.0                            ← 不再有 100% 空转线程
--- 原始 TCP：GET /?a=1 → HTTP/1.1 404 Not Found；GET /?a=1&b=2 → 404；GET /?= → 404；GET /?; → 404
```


### 10.4 残留 P0：查询串为空或全是分隔符时 → **整个服务进程段错误**（BUG-05R ②）

死循环消失后，同一段代码仍有一处未防护：改造后**第一次** `strtok_r` 也会返回 `NULL`（当 `query_ing` 为空或只由 `&` 组成时），而 `do{}` 循环体第一句就是 `strchr(line2, '=')` → 对 `NULL` 调用 `strchr` → SIGSEGV。

| 项目 | 内容 |
|---|---|
| 触发请求 | `GET /?`、`GET /?&`、`GET /?&&`（即"有问号但没有任何有效键值对"） |
| 后果 | **整个服务进程 core dump 退出**（不是单个线程/连接），一次请求即可远程打死服务 → 可用性 P0 |
| 位置 | `RequestCome/RequestAnalysis/RequestAnalysiser.c:112-115`（`strtok_r` 后 `do{}` 内 `strchr(line2,'=')` 无 NULL 判空） |
| 与第二版的关系 | 第二版是"死循环"，本轮修掉了死循环，但把崩溃点暴露成"空查询串"这一更窄、仍可 1 请求打崩进程的入口 |

**证据 1（原始 TCP 探针，绕过 curl 的 URL 规范化；`/tmp/e2e_v3c_out.txt`）**

```
server_pid=87979
[空查询串]  GET /?      | 响应首行=[] | 探针后存活=no   >>> 进程被杀（段错误）
[仅&]       GET /?&     | 响应首行=[] | 探针后存活=no   >>> 进程被杀（段错误）
[仅&&]      GET /?&&    | 响应首行=[] | 探针后存活=no   >>> 进程被杀（段错误）
[正常参数]  GET /?a=1   | 响应首行=[HTTP/1.1 404 Not Found] | 探针后存活=yes
[等号无值]  GET /?=     | 响应首行=[HTTP/1.1 404 Not Found] | 探针后存活=yes
[分号]      GET /?;     | 响应首行=[HTTP/1.1 404 Not Found] | 探针后存活=yes
[多参数]    GET /?a=1&b=2 | 响应首行=[HTTP/1.1 404 Not Found] | 探针后存活=yes
[编码空格]  GET /?%20   | 响应首行=[HTTP/1.1 404 Not Found] | 探针后存活=yes
```

**证据 2（curl 会把裸 `?` 规范化掉，容易误判为"没问题"）**

```
$ curl -sv -o /dev/null 'http://127.0.0.1:5343/?'
> GET / HTTP/1.1          ← curl 实际发出的请求行里 ? 已被去掉
```

所以第二版 9.3 里用 curl 复现 `?` 崩溃时看到的是超时/异常；本轮改用 **`bash /dev/tcp` 原始 TCP** 才拿到真实请求行，`GET /? HTTP/1.1` 与 `GET /?& HTTP/1.1` 都让进程段错误（父 shell 明确打印 `段错误（核心已转储）`）。

**证据 3（新增 2 条回归用例，`test/src/test_integrity.c` 用例 5/6，走 `run_isolated` fork 隔离）**

`cd test && make run`（本轮把边界用例纳入后，日志 `/tmp/unit_run5.log`）：

```
========== test_integrity ==========
  [PASS] 初始化解析用内存池
  [PASS] MethodMatch 能识别 GET（不应崩溃）
  [PASS] Content-Length: 5 → 解析出 5
  [PASS] Content-Length: 2000000 → 解析出 2000000（413 判定依赖它）
       Url=/search，查询参数 q=cline → 命中
  [PASS] /search?q=cline&page=2 → Url=/search 且 q、page 都能查到
  [FAIL] 空查询串 /? → 安全返回 Url=/（不应崩溃）：调用它的进程被信号 11（Segmentation fault）终止 → 函数本身崩溃
  [FAIL] 纯分隔符 /?& → 安全返回 Url=/（不应崩溃）：调用它的进程被信号 11（Segmentation fault）终止 → 函数本身崩溃
---- test_integrity: PASS 5 / FAIL 2 ----
==== 存在失败/卡死用例 ====
```

| 指标 | 含新增边界用例（`/tmp/unit_run5.log`） |
|---|---|
| PASS / FAIL | **254 / 2** |
| HANG | **0** |
| 失败点 | 100% 集中在 `DealUrlAndQuery` 的空查询串路径（上述 2 条） |
| 套件退出 | RUN_EXIT=2（因这 2 条失败，属**预期**回归信号） |

**建议修复（`RequestAnalysiser.c:110-125` 改为先判空再进循环）**

```c
        //开始切查询条件
        char* save2 = NULL;
        char* line2 = strtok_r(query_ing, "&", &save2);   // 空查询串/纯分隔符时这里就是 NULL
        while (line2 != NULL) {                            // 必须 while 判空，不能 do-while
            char* m = strchr(line2, '=');                  // 只有 line2 != NULL 时才解引用
            if (m) {
                *m = '\0';
                int e1 = AnalysiserHashInsert(h->Query, h, line2, m + 1);
                if (e1 != 1) { *failReason = 400; return -1; }
            }
            line2 = strtok_r(NULL, "&", &save2);           // 之后必须传 NULL 才会推进
        }
```

> 也可在语义上显式决定：若认为"带 `?` 但无参数"属非法请求，则在 `line2 == NULL` 时 `*failReason = 400; return -1;`（同样必须先判空）。要点只有一个：**`line2 == NULL` 时不得再执行 `strchr(line2, …)`**。另外 `char *save2 = "";` 建议改为 `char *save2 = NULL;`（`strtok_r` 会写入该指针，语义更正确）。

### 10.5 第一版报告中仍未处理的项（第三版现状）

| 编号 | 级别 | 现状（第三版） |
|---|---|---|
| BUG-05R ② | **P0** | **本轮的阻塞项**：空查询串/纯分隔符 → 进程段错误（10.4） |
| BUG-07 | P2 | 仍未处理：`main.c` 未注册任何路由、`asd` 用 `%c` 接收 `char*`（UB），故端到端只能看到 404 |
| BUG-08 | P2 | 仍未处理：内存池分配结果 `e0` 未检查 |
| BUG-09 | P2 | 仍未处理：根 `Makefile` 的 `SRCS = $(wildcard …)` 会编入 `test/src/*.c`，导致根目录 `make` 冲突（复测时用 `--exclude=./test` 的副本构建，`build_rc=0`） |
| BUG-10 | P3 | 部分消解：BUG-03/BUG-06 对应的告警已消失，其余告警清单见 9.7（`ReceiverThread.c` 4 条等） |

### 10.6 第三版放行判定

- **相对第二版是明确进步**：11/11 用例全绿、0 FAIL、0 HANG，`GET /?a=1` 由"5s 无响应 + 100% CPU"恢复为 `404 / 0.05s`，查询串参数也能正确解析（BUG-05 与 BUG-05R ① 一并解决）。
- **但仍不建议对外提供服务**：`GET /?`（原始请求行）即可让**整个进程 core dump**，属一行代码可修的远程 DoS。修掉 10.4 后建议再走一次 10.7 的回归。
- **修完后的回归门槛**：`cd test && make run` 必须 **0 FAIL / 0 HANG**（当前 `test_integrity` 会因新增的 2 条边界用例报 FAIL，修好即自动转 PASS），并复做端到端：`GET /`、`GET /?a=1`、原始 TCP `GET /?`、`GET /?&`（四项都必须回包且进程存活）。

### 10.7 第三版原始日志索引

| 用途 | 文件 |
|---|---|
| 第三版套件（11/11 全绿，`RUN_EXIT=0`） | `/tmp/unit_run4.log` |
| 第三版套件（含新增 2 条边界用例，254 PASS / 2 FAIL） | `/tmp/unit_run5.log` |
| 第三版源码 diff | `/tmp/diff_v3.txt` |
| 工作区指纹（mtime + sha256 + git status） | `/tmp/v3_state.txt` |
| 端到端（curl，`/` `/?a=1` `/?q=…`） | `/tmp/e2e_v3_out.txt` |
| 端到端（原始 TCP 探针，含 3 个崩溃请求） | `/tmp/e2e_v3c_out.txt` |
| 套件明细 / 崩溃定位辅助 | `/tmp/run4_detail.txt`、`/tmp/p5.txt`、`/tmp/ti_run5.txt` |
| 构建产物信息 | `/tmp/main_stat.txt` |

> 第三版复测同样**只读**运行被测代码，未改动那 4 个源文件（sha256 见 10.1）。本轮我修改的仍只有自己的测试资产：`test/src/test_integrity.c` 新增 2 条边界回归用例（`空查询串 /?`、`纯分隔符 /?&`，走已有的 `run_isolated` fork 隔离）。


---

## 11. 第四次复测（2026-10-14：空查询串防护未生效）

> 本章针对用户**第三次修改**（仅 `RequestAnalysiser.c` 增改一行）的复测结论。**结论：该防护未生效，第 10.4 节的 P0 崩溃依然存在。**

### 11.1 复测对象与指纹

| 文件 | 修改时间 | SHA-256（前 16 位） | 本轮改动 |
|---|---|---|---|
| `RequestCome/RequestAnalysis/RequestAnalysiser.c` | 2026-10-14 14:14:09 | `1a7c957230d0ee2d` | 新增第 111 行 `if(query_ing == "") return 1;` |
| `StaterMain/StarterMain.c` | 2026-10-13 22:42:58 | `a54524f022ca4511` | 未再改 |
| `FdConnectManage/EpollOperate/EpollOperate.c` | 2026-10-13 22:39:16 | `4b572fce63e29a0c` | 未再改 |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | 2026-10-13 22:40:35 | `b64a8886cf4dd2ea` | 未再改 |

```diff
         //开始切查询条件
+        if(query_ing == "") return 1;
         char *save2 = "";
         char* line2 = strtok_r(query_ing, "&", &save2);
         do{
             char* m=strchr(line2,'=');        // ← 仍然没有任何 line2 == NULL 的判断
```

### 11.2 复测结果：与第三版**完全一致**，问题未解决

| 指标 | 第四版 | 第三版 |
|---|---|---|
| PASS / FAIL（套件） | **254 / 2** | 254 / 2 |
| HANG | 0 | 0 |
| `test_parser` | PASS 25 / FAIL 0 | PASS 25 / FAIL 0 |
| `test_integrity` | PASS 5 / FAIL 2 | PASS 5 / FAIL 2 |
| 原始 TCP `GET /?` | **进程段错误（core dumped）** | 进程段错误 |
| 原始 TCP `GET /?&` | **进程段错误（core dumped）** | 进程段错误 |
| 原始 TCP `GET /?&&` | **进程段错误（core dumped）** | 进程段错误 |
| 原始 TCP `GET /?a=1`、`/?a=1&b=2`、`/?;` | 404 / 正常，进程存活 | 正常 |

日志：`/tmp/unit_run6.log`（RUN_EXIT=2）、`/tmp/e2e_v4_out.txt`。**第三版全绿的 11 个用例仍然全绿、无 HANG，说明这次改动没有破坏任何既有功能**，只是没修到点子上。

### 11.3 未生效的根本原因：`query_ing == ""` 比较的是**指针地址**，不是字符串内容

`query_ing` 指向请求缓冲区（第 99 行 `char* query_ing = path_cut + 1;`），而 `""` 是 `.rodata` 里的字面量地址；两者永远不相等，所以这条 `return 1;` **一次都不会执行**。

| 证据 | 内容 |
|---|---|
| ① 编译告警（`/tmp/unit_run6.log`） | `../RequestCome/RequestAnalysis/RequestAnalysiser.c:111:22: warning: comparison with string literal results in unspecified behavior [-Waddress]` |
| ② 最小实验（`/tmp/ptr_cmp.c` → `/tmp/ptr_cmp_demo.txt`） | 空串输入：`if (query_ing == "")` → **假**；`字面量""地址=0x57d1a76a9008  query_ing指向=0x7ffcf3bf1dd0`（地址不同）<br>纯 `&` 输入：`if (query_ing == "")` → **假**；`*query_ing=='\0'` → **假**；`strtok_r("&","&")` → **NULL** |
| ③ 端到端（`/tmp/e2e_v4_out.txt`） | 若守卫生效，`GET /?` 应回 `404`；实测三次请求都让服务进程段错误退出 |

### 11.4 正确修法

⚠️ **注意**：即便把那行写对成"内容判空"（`if (*query_ing == '\0') return 1;`），也只能挡住 `/?`，**挡不住 `/?&`、`/?&&`**——因为此时 `query_ing` 是 `"&"`（非空），首轮 `strtok_r` 仍返回 `NULL`。真正稳的做法是**在拿到第一个 token 后判空**：

```c
        //开始切查询条件
        char* save2 = NULL;
        char* line2 = strtok_r(query_ing, "&", &save2);
        if (line2 == NULL) return 1;          // ← 最小补丁：覆盖 ""、"&"、"&&" 等全部空/纯分隔符情况
        do{
            char* m = strchr(line2, '=');     // 只有 line2 != NULL 时才解引用
            if (m) {
                *m = '\0';
                int e1 = AnalysiserHashInsert(h->Query, h, line2, m + 1);
                if (e1 != 1) { *failReason = 400; return -1; }
            }
            line2 = strtok_r(NULL, "&", &save2);
        }while (line2 != NULL);
```

或等价的"把 `do{}while` 换成 `while () {}`"写法（见 10.4）。两种写法都可，**关键只有一点：`strchr(line2,'=')` 之前必须保证 `line2 != NULL`**。另外 `char *save2 = "";` 建议写成 `char *save2 = NULL;`（`strtok_r` 会写入该指针，写成指针更符合语义）。

### 11.5 第四版放行判定与日志

- **功能面**：`test_parser` 25 条、`test_epoll` 10 条等全部保持通过，查询串正常场景端到端正常 → **修复尝试没有引入新问题**。
- **阻塞面**：`GET /?` 一个原始请求即可让整个进程 core dump，**仍不建议对外提供服务**。
- **回归门槛（修好后必须全部满足）**：`cd test && make run` → **254 PASS / 0 FAIL / 0 HANG、RUN_EXIT=0**（`test_integrity` 那 2 条会转 PASS），且 `/tmp/e2e_v4.sh` 里 6 个探针全部"进程存活=yes"。

| 用途 | 文件 |
|---|---|
| 第四版套件日志（254 PASS / 2 FAIL / 0 HANG） | `/tmp/unit_run6.log` |
| 第四版源码 diff | `/tmp/diff_v4.txt` |
| 第四版工作区指纹 | `/tmp/v4_state.txt` |
| 第四版端到端（原始 TCP，含 3 个崩溃探针） | `/tmp/e2e_v4_out.txt`、`/tmp/e2e_v4_build.log` |
| 指针比较最小实验 | `/tmp/ptr_cmp.c`、`/tmp/ptr_cmp_demo.txt` |


---

## 12. 第五次复测（2026-10-14：空查询串崩溃已修复 ✅）

> 本章针对用户**第四次修改**（仅 `RequestAnalysiser.c` 增一行）的复测结论。**结论：第 10.4 / 11 章的 P0 崩溃已修复，单元用例与端到端探针全绿。**

### 12.1 复测对象与指纹

| 文件 | 修改时间 | SHA-256（前 16 位） | 本轮改动 |
|---|---|---|---|
| `RequestCome/RequestAnalysis/RequestAnalysiser.c` | 2026-10-14 14:29:16 | `1dafd75d406e962c` | 首个 `strtok_r` 之后新增 `if(line2 == NULL) return 1;`（即 11.4 给出的最小补丁） |
| `StaterMain/StarterMain.c` | 2026-10-13 22:42:58 | `a54524f022ca4511` | 未再改 |
| `FdConnectManage/EpollOperate/EpollOperate.c` | 2026-10-13 22:39:16 | `4b572fce63e29a0c` | 未再改 |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | 2026-10-13 22:40:35 | `b64a8886cf4dd2ea` | 未再改 |

```c
        //开始切查询条件
        char *save2 = "";
        char* line2 = strtok_r(query_ing, "&", &save2);
        if(line2 == NULL) return 1;        // ★ 本轮新增：空查询串 / 纯分隔符在此安全返回
        do{ … line2 = strtok_r(NULL, "&", &save2); }while (line2 != NULL);
```

### 12.2 套件结果：256 PASS / 0 FAIL / 0 HANG，11 / 11 全绿

`cd test && make run`（日志 `/tmp/unit_run7.log`，`RUN_EXIT=0`，末行 `==== 全部单元测试通过 ====`）

| 用例 | 第五版 | 第四版 |
|---|---|---|
| `test_memory_pool` | PASS 27 / FAIL 0 | PASS 27 |
| `test_rwlock` | PASS 12 / FAIL 0 | PASS 12 |
| `test_hash` | PASS 30 / FAIL 0 | PASS 30 |
| `test_queues` | PASS 36 / FAIL 0 | PASS 36 |
| `test_parser` | PASS 25 / FAIL 0 | PASS 25 |
| `test_response` | PASS 18 / FAIL 0 | PASS 18 |
| `test_temp_pool` | PASS 40 / FAIL 0 | PASS 40 |
| `test_timer` | PASS 36 / FAIL 0 | PASS 36 |
| `test_router` | PASS 15 / FAIL 0 | PASS 15 |
| `test_integrity` | **PASS 7 / FAIL 0** | PASS 5 / FAIL 2 |
| `test_epoll` | PASS 10 / FAIL 0 | PASS 10 |

| 指标 | 第五版 | 第四版 | 第三版 |
|---|---|---|---|
| 通过断言 | **256** | 254 | 254 |
| 失败断言 | **0** | 2 | 2 |
| 卡死用例 | **0** | 0 | 0 |
| 全绿用例 | **11 / 11** | 10 / 11 | 10 / 11 |
| 套件退出 | **RUN_EXIT=0** | RUN_EXIT=2 | RUN_EXIT=2 |

原第四版 FAIL 的 2 条边界用例已转 PASS（原文）：

```
       原始 url=[/?] → Url=[/]，查询表=空
  [PASS] 空查询串 /? → 安全返回 Url=/（不应崩溃）
       原始 url=[/?&] → Url=[/]，查询表=空
  [PASS] 纯分隔符 /?& → 安全返回 Url=/（不应崩溃）
---- test_integrity: PASS 7 / FAIL 0 ----
```

### 12.3 端到端：10 个原始 TCP 探针全绿 + 30 轮压测（`/tmp/e2e_v5_out.txt`）

```
[空查询串] GET /?        → HTTP/1.1 404 Not Found | 存活=yes   ← 第四版此处 core dump
[纯&]      GET /?&       → HTTP/1.1 404 Not Found | 存活=yes
[纯&&]     GET /?&&      → HTTP/1.1 404 Not Found | 存活=yes
[纯&&&]    GET /?&&&     → HTTP/1.1 404 Not Found | 存活=yes
[正常参数] GET /?a=1      → HTTP/1.1 404 Not Found | 存活=yes
[多参数]   GET /?a=1&b=2  → HTTP/1.1 404 Not Found | 存活=yes
[等号无值] GET /?=        → HTTP/1.1 404 Not Found | 存活=yes
[分号]     GET /?;        → HTTP/1.1 404 Not Found | 存活=yes
[编码空格] GET /?%20      → HTTP/1.1 404 Not Found | 存活=yes
[对照]     GET /          → HTTP/1.1 404 Not Found | 存活=yes
--- 压力：连续 30 次 /? 与 /?& ---
  30 轮混合空查询串后：进程存活=yes
[压测后 /?a=1]            → HTTP/1.1 404 Not Found | 存活=yes
```

> 端到端响应码是 `404` 而不是 `200`，原因是 `main.c` **没有注册任何路由**（BUG-07），与本次修复无关；本轮要验证的是"**进程不再崩溃、连接能被正常处理并回包**"。

### 12.4 告警面：`-Waddress` 已消失

第五版的 `-Wall` 输出只剩两条与本次无关的既有告警：`RequestAnalysiser.c:19 unused variable 'e0'`（= BUG-08）、`MemoryPool.c:42 control reaches end of non-void function`（= BUG-10 一类）。第四版那条 `comparison with string literal results in unspecified behavior [-Waddress]` 已随本次修改消失。

### 12.5 累计修复确认清单（跨三次复测）

| 缺陷 | 状态 | 关键证据 |
|---|---|---|
| BUG-01 监听 fd 未回传 | ✅ 已修 | e2e `GET /` → 404 / 0.05s、连接被 accept |
| BUG-02 `method=NULL` | ✅ 已修 | `test_temp_pool` 40 PASS |
| BUG-03 `EPOLLIN \|\| EPOLLOUT` | ✅ 已修 | `test_epoll` 10 PASS |
| BUG-04 `Content-Length` 双重偏移 | ✅ 已修 | `test_integrity` CL 5 / 2000000 |
| BUG-05 查询参数全部丢失 | ✅ 已修 | `test_parser`：`q=cline`、`page=2` 命中 |
| BUG-05R ① 查询串死循环 | ✅ 已修 | 第三版 254/0/0 + e2e `/?a=1` 0.05s |
| BUG-05R ② 空查询串崩进程 | ✅ **本版已修** | `test_integrity` 7 PASS + 10 探针 + 30 轮压测 |
| BUG-06 `StartThread` 缺 return | ✅ 已修 | `-Wreturn-type` 告警消失 |
| BUG-07 无路由注册 / `%c` 传 `char*` | ❌ 未处理 | e2e 全部 404；`main.c:4` `-Wformat=` 告警 |
| BUG-08 分配结果未检查 | ❌ 未处理 | `-Wunused-variable 'e0'` |
| BUG-09 根 Makefile 通配冲突 | ❌ 未处理 | 需 `filter-out test/%`（见 test/Makefile 注释） |
| BUG-10 静态告警清单 | ⚠️ 部分 | 仍有 20+ 条（9.7 清单） |

### 12.6 第五版放行判定

- **P0 全部清零**：可用性三连（不处理请求 → 死循环 → 进程崩溃）已全部修复；**256 PASS / 0 FAIL / 0 HANG，11/11 全绿**。
- **结论升级为：可以做功能联调/内测**（不再是"不能对外"）。正式对外前建议先处理 **BUG-07**（业务路由未注册，所有请求都是 404）、BUG-08、BUG-09。
- **回归门槛已达成**（对照 11.5）：`cd test && make run` → 0 FAIL / 0 HANG / `RUN_EXIT=0` ✅；端到端 6 个探针全部"进程存活=yes" ✅（实际做了 10 个探针 + 30 轮压测）。
- 建议把 `make -C test run`（一条命令、退出码即结论）纳入后续每次改动的自检流程。

### 12.7 第五版日志索引

| 用途 | 文件 |
|---|---|
| 第五版套件（256 PASS / 0 FAIL / 0 HANG，RUN_EXIT=0） | `/tmp/unit_run7.log` |
| 第五版源码 diff | `/tmp/diff_v5.txt` |
| 第五版工作区指纹 + 查询串源码片段 | `/tmp/v5_state.txt` |
| 第五版端到端（原始 TCP，10 探针 + 30 轮压测） | `/tmp/e2e_v5_out.txt`、`/tmp/e2e_v5_build.log` |
| `test_integrity` 明细（含 2 条转 PASS 的边界用例） | `/tmp/run7_detail.txt` |

> 第五版复测同样**只读**运行被测代码（4 个源文件 sha256 见 12.1），本轮我未修改 `test/` 下任何文件。


## 13. 网站与示例应用端到端测试（2026-10-14：新增 `WebSite/` + `WebApp/`，修复 BUG-07 / BUG-09）

> 本章针对**本轮新增的示例网站与应用层**。框架逻辑（`StaterMain/`、`Router/`、`RequestAndResponse/`、`MemoryPool/`、`ResponseSend/`、`FdConnectManage/`、`Queue/`）**零改动**，4 个用户源文件的 SHA-256 与 12.1 逐字节一致。
> 验收脚本：`test/website_test.sh`（164 行）。
> **结论：网站测试 27 PASS / 0 FAIL；单元套件 256 PASS / 0 FAIL / 0 HANG（未回归）。** BUG-07 与 BUG-09 在本轮一并修复。

### 13.1 本轮新增与修改的文件

| 文件 | 行数 | 类型 | 作用 |
|---|---|---|---|
| `WebApp/WebApp.c` | 261 | 新增 | 路由注册 + 12 个业务 handler（静态文件 / JSON / 256KB / sendfile） |
| `WebApp/WebApp.h` | 9 | 新增 | 暴露 `WebAppRegisterRoutes(void)`（签名与框架 `RouterFunc` 一致） |
| `WebSite/index.html` | 87 | 新增 | 首页（含断言要求的 `c_web is running` 字样） |
| `WebSite/about.html` | 61 | 新增 | 关于页 |
| `WebSite/style.css` | 159 | 新增 | 样式（纯本地，无 CDN 依赖，可离线打开） |
| `WebSite/app.js` | 91 | 新增 | 前端在线自测：调用 `/time`、`/echo`、`/api/info`、`/mem/256k`、`/no-such-route` |
| `WebSite/favicon.ico` | 1118 B | 新增 | 真实 16×16 32bpp ICO（`file` 识别为 `MS Windows icon resource - 1 icon, 16x16 with 32-bit`） |
| `WebSite/static/10mb.bin` | 10485760 B | 新增 | sendfile 下载断言用文件 |
| `main.c` | 11 | **修改** | 删除坏桩 `asd()`（`printf("%c","hello world")` 类型错），改为 `WebStater("127.0.0.1",5343,WebAppRegisterRoutes)` |
| `Makefile` | +2 | **修改** | BUG-09 修复：`SRCS := $(filter-out test/%,$(SRCS))` |
| `test/website_test.sh` | 164 | 上一轮新增 | 本轮未改，作为验收脚本直接复用 |
| `.vscode/tasks.json` | +1 | **修改** | 把 `WebApp/WebApp.c` 加入 VS Code `build` 任务的文件列表；否则 `preLaunchTask: build`（F5 调试）会 `undefined reference to WebAppRegisterRoutes` |

### 13.2 BUG-07 / BUG-09 处理结果

| 缺陷 | 原状（见第 5 章） | 本轮处理 | 验证证据 |
|---|---|---|---|
| **BUG-07**（P2 次要） | `main.c` 只有一个类型错误的空桩，`RouterAppend` 从未被调用 → **所有请求 404** | 新增 `WebApp/WebApp.c` 注册 14 条路由，`main.c` 把注册函数作为 `RouterFunc` 回调传给 `WebStater`（框架在 `RouterInit()` 之后、启动线程之前调用，注册天然线程安全） | `GET /`：`404 Not Found` → **`200 OK` / 3136 字节 HTML** |
| **BUG-09**（P2 集成） | 根 `make` 用 `wildcard` 把 `test/src/*.c` 一起编入 → 13 个 `multiple definition of 'main'`，`make` **rc=2**（附录 C） | 根 Makefile 增加注释行 + `SRCS := $(filter-out test/%,$(SRCS))`（共 2 行，见该文件 17–19 行） | 全量重编（先 `rm -f $(find . -name '*.o') main`）**`make` rc=0**，产出 `main` 185624 字节；`make -C test run` 仍独立可用 |

### 13.3 路由表与断言映射

| 路由 | 方法 | 响应内容 | 对应断言 |
|---|---|---|---|
| `/`、`/index.html` | GET | `WebSite/index.html`（`text/html; charset=utf-8`） | 200 + 正文含 `c_web is running` + Content-Type |
| `/about`、`/about.html` | GET | `WebSite/about.html` | 补充人工验证（200，2323 字节） |
| `/style.css` / `/app.js` / `/favicon.ico` | GET | 对应磁盘文件 | 补充人工验证（`text/css` / `application/javascript` / `image/x-icon`） |
| `/hello` | GET | `Hello, c_web!` | 正文精确匹配（13 字节） |
| `/time` | GET | `YYYY-MM-DD HH:MM:SS`（`localtime_r` + `strftime`） | 正则匹配真实时间 |
| `/echo?name=&msg=` | GET | `{"name":"..","msg":".."}`（应用层先 URL 解码、再 JSON 转义） | 查询串回显正确 |
| `/api/echo` | POST | `{"method":"POST","body":"<请求体>"}` | POST 请求体回显正确 |
| `/api/echo` | GET | `405 Method Not Allowed` | 仅允许 POST（405） |
| `/api/info` | GET | `{"server":"c_web","pid":..,"uptime":..,"port":..}` | 补充人工验证（首页状态徽标消费它） |
| `/mem/256k` | GET | 262144 字节文本（BSS 静态体，`strlen` 恰好 262144） | 状态码 + 落盘 262144 字节 |
| `/download/10mb` | GET | `WebSite/static/10mb.bin`（`sendfile` 零拷贝） | 10485760 字节 + sha256 一致 + `Content-Length` |
| 其它任意路径 | * | 框架统一 `404` + 正文 `Not Found` | 未注册路由断言 |

> 框架约定：`Content-Length` 由打包阶段统一生成（`Content-Length: 200 OK` 文案由框架负责），**应用只设 `Content-Type`**；
> 文件响应的 `Content-Length` 由框架 `fstat` 自动填充（已实测 10485760）。`ResponseBodySet()` 内部按 `strlen` 取长度，故应用层响应体必须以 `'\0'` 结尾。

### 13.4 网站端到端测试结果：PASS 27 / FAIL 0（`bash test/website_test.sh`，原始输出）

```
== 0.构建 ==
  [PASS] 10MB 测试文件大小（/home/yaoxianglongde/c_code/c_web/WebSite/static/10mb.bin）（10485760）
== 1.启动服务器 ==
  [PASS] 服务器已在 http://127.0.0.1:5343 就绪（pid 96461）
== 2.路由功能 ==
  [PASS] GET / 状态码（200）
  [PASS] / 返回包含 c_web is running 的首页
  [PASS] 首页 Content-Type 为 text/html
  [PASS] GET /hello 状态码（200）
  [PASS] /hello 响应体（Hello, c_web!）
  [PASS] GET /time 状态码（200）
  [PASS] /time 返回真实时间（2026-10-14 16:07:11）
  [PASS] GET /echo 状态码（200）
  [PASS] 查询串回显正确（{"name":"cline","msg":"hello"}）
  [PASS] POST /api/echo 状态码（200）
  [PASS] POST 请求体回显正确
  [PASS] GET /api/echo → 405（仅允许 POST）（405）
  [PASS] 未注册路由 → 404（404）
  [PASS] 404 响应体（Not Found）
  [PASS] 256KB 内存响应（状态码 + 下载字节数）（200 262144）
  [PASS] 256KB 响应实际落盘大小（262144）
== 3.10MB 大文件下载 ==
  [PASS] 下载状态码（200）
  [PASS] 下载字节数（10MiB = 10485760）（10485760）
  [PASS] 单次下载耗时 0.070046 s，速度 142.8 MB/s
  [PASS] 下载内容与源文件逐字节一致（sha256 977c3684d1b686fa…）
  [PASS] Content-Length 响应头（框架由 fstat 自动填充）（10485760）
  [PASS] 第二次下载仍完整（验证发送暂存池回收）（200 10485760）
  [PASS] 第二次下载内容一致
== 4.并发下载（4 路 × 10MB） ==
  [PASS] 4 路并发 10MB 下载全部完整且内容一致
  (4×10MB 并发耗时 0.09 s，总吞吐约 424.3 MB/s)
== 5.收尾检查 ==
  [PASS] 全部请求结束后服务器进程仍存活（无崩溃）

==== 网站测试结果：PASS 27 / FAIL 0 ====
```

- 构建阶段 `make` 无 error / 无 warning（`WebApp/WebApp.c` 在 `-g -O0 -Wall` 下零告警）。
- 其中 8 条是**内容一致性**断言（首页关键字、`/hello` 正文、查询串回显、POST 体回显、404 正文、10MB sha256 逐字节比对、二次下载比对、4 路并发内容比对），不只是看状态码和长度。
- 两次下载 + 4 路并发下载同时覆盖了"发送暂存池回收 → 二次发送"路径。

### 13.5 单元套件未回归：256 PASS / 0 FAIL / 0 HANG（11/11 用例）

新增 `WebApp/` 与 Makefile 改动后重新执行 `make -C test run`（`test/Makefile` 独立，不受根 Makefile 影响）：

| 用例 | 结果 | 用例 | 结果 |
|---|---|---|---|
| `test_memory_pool` | PASS 27 / FAIL 0 | `test_temp_pool` | PASS 40 / FAIL 0 |
| `test_rwlock` | PASS 12 / FAIL 0 | `test_timer` | PASS 36 / FAIL 0 |
| `test_hash` | PASS 30 / FAIL 0 | `test_router` | PASS 15 / FAIL 0 |
| `test_queues` | PASS 36 / FAIL 0 | `test_integrity` | PASS 7 / FAIL 0 |
| `test_parser` | PASS 25 / FAIL 0 | `test_epoll` | PASS 10 / FAIL 0 |
| `test_response` | PASS 18 / FAIL 0 | **合计** | **256 PASS / 0 FAIL / 0 HANG** |

日志末尾：`==== 全部单元测试通过 ====`，`make -C test run` 退出码 **0**（日志 `/tmp/unit_after_app.log`）。与第 12 章的 256 PASS 数量、11 个用例分布**完全一致**，可判定框架逻辑无回归。

### 13.6 补充人工验证（原始 TCP / curl 细节，`/tmp/live_app.txt`、`/tmp/live_app2.txt`）

静态资源与路由边界：

| 请求 | 状态码 | Content-Type | 长度 |
|---|---|---|---|
| `/index.html`、`/` | 200 | `text/html; charset=utf-8` | 3136 |
| `/about`、`/about.html` | 200 | `text/html; charset=utf-8` | 2323 |
| `/style.css` | 200 | `text/css; charset=utf-8` | 3309 |
| `/app.js` | 200 | `application/javascript; charset=utf-8` | 3372 |
| `/favicon.ico` | 200 | `image/x-icon` | 1118 |
| `/?a=1&b=2` | 200 | `text/html; charset=utf-8` | 3136（查询串不影响路由匹配） |
| `/download/10mb` | 200 | `application/octet-stream` | 10485760 |
| `/mem/256k` | 200 | `text/plain; charset=utf-8` | 262144 |

方法/错误码：

```
GET  /api/echo  -> 405
POST /api/echo  -> 200 {"method":"POST","body":"ping-body"}
HEAD /          -> 405
POST /hello     -> 405
GET  /nope      -> 404  正文 "Not Found"
```

应用层 URL 解码与转义（框架 `RequestGetter("Query",k)` 只给原始串，解码在 `WebApp.c` 内完成）：

| 查询串 | 响应正文 |
|---|---|
| `name=%E4%B8%AD%E6%96%87%E7%BD%91%E7%AB%99&msg=hello` | `{"name":"中文网站","msg":"hello"}` |
| `name=a+b&msg=%22q%22` | `{"name":"a b","msg":"\"q\""}` |
| `name=%E4%B8%AD&msg=%E4%B8%AD` | `{"name":"中","msg":"中"}` |
| `name=100%25&msg=%ZZ%` | `{"name":"100%","msg":"%ZZ%"}`（非法转义原样保留，不崩） |
| `name=%E4`（截断的 UTF-8 序列） | 200，原样透传 1 字节，不崩 |
| `name=` + 300 个 `A` | 200，响应 269 字节（应用层 255 字节缓冲上限截断，无溢出） |

### 13.7 并发压测：字节级对账

`xargs -P 20` 打 200 次混合请求（`/` : `/echo?name=x&msg=y` : `/mem/256k` : `POST /api/echo` = 各 50 次），只统计返回码与字节数：

```
HTTP 200 : 200 次, 合计 13266550 字节
理论值 = 50 × (3136 + 22 + 262144 + 29) = 50 × 265331 = 13,266,550 字节
（22 = {"name":"x","msg":"y"}，29 = {"method":"POST","body":"ab"}）
```

→ 实测与理论**逐字节相等**，说明 200 个并发响应没有丢包、没有串包、没有截断，`/mem/256k` 每次都是准 262144 字节。

```
8 路并发 10MB 下载： HTTP 200 : 8 次, 合计 83886080 字节 = 8 × 10485760 ✅
压测结束后进程：      STAT=Sl  %CPU=2.3  RSS=4864KB  NLWP=3  仍存活
收尾：                pgrep -x main → 无残留；ss -ltn 无 5343 监听 ✅
```

### 13.8 已知限制（本轮未做，或依赖框架改造）

| 限制 | 现状 | 说明 / 建议 |
|---|---|---|
| `HEAD` 请求 | `/` → **405** | 框架按 `Method + Url` 精确匹配路由，且 `Content-Length` 由打包阶段统一生成，应用层无法"只发头不发体"；要支持 HEAD 需框架层面按方法跳过 body |
| Query 值不做 URL 解码 | 框架层原样返回 | 示例应用在 `WebApp.c` 里补了 `UrlDecode()`（`%XX` + `+`→空格，非法转义原样保留）；若希望统一语义，建议下沉到 `RequestAnalysiser` |
| `/echo` 参数超长 | 应用层 255 字节缓冲，超出截断 | 示例代码的显式上限，不会溢出 |
| 静态文件缺失 | 该路由返回 404 | 应用层策略（`open` 失败 → `ResponseErrorSet(404)`）；例如未放 `favicon.ico` 时就是 404 |
| 网站目录是相对路径 | `WebSite/`（相对工作目录） | **必须在仓库根目录启动 `./main`**，否则静态路由 404（`/hello`、`/time` 等纯动态路由不受影响） |
| Range / gzip / keep-alive 复用策略 | 未实现 | 框架与示例均无；大文件下载是整文件 `sendfile` |
| BUG-08 / BUG-10 | 仍未处理 | `e0` 未检查、20+ 条静态告警；**本轮新增的 `WebApp/WebApp.c` 在 `-Wall` 下零告警** |

### 13.9 第六版放行判定与日志索引

- **功能联调 / 内测：可以。** 示例网站 + 动态接口 **27/27 全绿**，框架单元 **256/256 全绿**，200 并发混合请求字节级对账一致，全程无崩溃、无残留进程。
- **对外提供服务前**仍建议：处理 BUG-08 / BUG-10；补齐 `HEAD`、`Range`、压缩等 HTTP 语义；把 `WebSite/` 路径改为可配置（或绝对路径）。
- **两条构建入口都已实测可用**：① 根 `make`（rc=0）；② VS Code `build` 任务（显式文件列表，已加入 `WebApp/WebApp.c`，实测 rc=0 且零输出）。`launch.json` 的 `program` 指向 `${workspaceFolder}/main`，与 `Makefile` 的输出一致，**F5 可直接调试**（已用该入口构建的二进制跑通全部 12 条路由冒烟：`/`3136、`/about`2323、`/style.css`3309、`/app.js`3372、`/favicon.ico`1118、`/hello`13、`/time`19、`/echo`30、`/api/info`53、`/mem/256k`262144、`/download/10mb`10485760、`/nope`404）。
- 建议把下面两条命令纳入日常自检（都有明确退出码，适合脚本/CI）：
  ```bash
  make -C test run            # 单元 + 组件级：256 PASS / 0 FAIL / 0 HANG
  bash test/website_test.sh   # 网站端到端：PASS 27 / FAIL 0
  ```

| 用途 | 文件 |
|---|---|
| 全量重编（`make` rc=0，WebApp 零告警） | `/tmp/build_app.txt` |
| 网站端到端测试（构建 + 27 条断言） | `/tmp/round3.txt` |
| 并发压测（200 混合 + 8×10MB，字节对账） | `/tmp/stress_out.txt`、`/tmp/stress_codes.txt` |
| 人工验证第一轮（静态资源、头部、方法/错误码） | `/tmp/live_app.txt` |
| 人工验证第二轮（favicon、`/about.html`、URL 解码、超长参数） | `/tmp/live_app2.txt` |
| 新增代码后单元套件（256 PASS / 0 FAIL） | `/tmp/unit_after_app.log` |
| 文件行数、4 个用户源文件 sha256、Makefile diff | `/tmp/ch13_data.txt` |
| 最终验收（VS Code 构建入口 + `make` + 27 条断言 + 收尾检查） | `/tmp/final_accept.txt`、`/tmp/vsc_build.txt` |

> 本章所有操作均在**仓库工作区内**完成；除 `main.c`（11 行）与根 `Makefile`（+2 行）外，**未改动任何原有源码**，4 个用户源文件 SHA-256 与 12.1 完全一致（`a54524f0…`、`b64a8886…`、`1dafd75d…`、`4b572fce…`）。
> 另注：`main` 二进制在仓库中被 git 跟踪，重新构建后处于已修改状态（`git status` 显示 ` M main`），这是构建产物更新，非源码改动。

## 14. 附录

### 附录 A：端到端原始输出（`/tmp/e2e_orig.log`）

```
=== 服务进程 ===
72149 /tmp/cweb_orig
=== 监听状态（curl 前）===
LISTEN 0      100        127.0.0.1:5343       0.0.0.0:*
=== curl GET / (超时 5s) ===
--- curl 进行中：监听套接字接收队列（Recv-Q 未 accept 的连接数）---
LISTEN 1      100        127.0.0.1:5343       0.0.0.0:*
--- curl 进行中：已建立连接 ---
0      0           127.0.0.1:49044      127.0.0.1:5343
78     0           127.0.0.1:5343       127.0.0.1:49044
curl: (28) Operation timed out after 5003 milliseconds with 0 bytes received
code=000 size=0 time=5.003686
curl_exit=28
=== 服务进程是否崩溃 ===
72149 /tmp/cweb_orig            ← 进程仍在，未崩溃
=== 服务器 stderr/stdout ===    ← 无任何输出
```

### 附录 B：9 条失败断言的原文（`/tmp/unit_orig2.log`）

```
  [FAIL] Query 参数 q=cline  (src/test_parser.c:55)
  [FAIL] Query 参数 page=2  (src/test_parser.c:57)
  [FAIL] 有效查询参数 x=1 被解析  (src/test_parser.c:136)
  [FAIL] MethodMatch 能识别 GET（不应崩溃）：调用它的进程被信号 11（Segmentation fault）终止 → 函数本身崩溃
  [FAIL] Content-Length: 5 → 解析出 5：返回结果与预期不符（子进程退出码 2）
  [FAIL] Content-Length: 2000000 → 解析出 2000000（413 判定依赖它）：返回结果与预期不符（子进程退出码 2）
  [FAIL] /search?q=cline&page=2 → Url=/search 且 q、page 都能查到：返回结果与预期不符（子进程退出码 3）
  [FAIL] EpollChangeReAndWr 后必须收到 EPOLLOUT（写端可写）  (src/test_epoll.c:37)
  [FAIL] 缓冲腾出空间后必须收到 EPOLLOUT 以继续发送剩余数据  (src/test_epoll.c:59)
```

各用例汇总行（原文）：

```
---- test_memory_pool: PASS 27 / FAIL 0 ----
---- test_rwlock: PASS 12 / FAIL 0 ----
---- test_hash: PASS 30 / FAIL 0 ----
---- test_queues: PASS 36 / FAIL 0 ----
---- test_parser: PASS 22 / FAIL 3 ----
---- test_response: PASS 18 / FAIL 0 ----
========== test_temp_pool ==========
Segmentation fault (core dumped)          ← 0 条断言即崩溃（BUG-02）
---- test_timer: PASS 36 / FAIL 0 ----
---- test_router: PASS 15 / FAIL 0 ----
---- test_integrity: PASS 1 / FAIL 4 ----
---- test_epoll: PASS 8 / FAIL 2 ----
==== 存在失败用例 ====
make: *** [Makefile:61：run] 错误 1
RUN_EXIT=2
```

### 附录 C：构建冲突原始输出（`/tmp` 副本执行根 `make`）

```
/tmp/cweb_mk/test/src/test_epoll.c:13: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_hash.c:17: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_integrity.c:120: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_memory_pool.c:14: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_parser.c:34: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_queues.c:67: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_response.c:41: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/usr/bin/x86_64-linux-gnu-ld.bfd: test/src/test_router.o:/tmp/cweb_mk/test/src/test_router.c:13:
    multiple definition of `router'; GlobalResource/GlobalResource.o:/tmp/cweb_mk/GlobalResource/GlobalResource.c:5: first defined here
/tmp/cweb_mk/test/src/test_router.c:20: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
/tmp/cweb_mk/test/src/test_rwlock.c:104: multiple definition of `main'; main.o:/tmp/cweb_mk/main.c:6: first defined here
（另有 test_timer.c、test_temp_pool.c 同类错误）
```

### 附录 D：环境与复现入口

| 项目 | 值 |
|---|---|
| 版本 | `314174f`（分支 `main`） |
| 内核 / 架构 | Linux 7.0.0-31-generic / x86_64 |
| 编译器 | gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0，`-g -O0 -Wall` |
| 一键跑全部用例 | `make -C test run` |
| 单个用例复现 | `cd test && make build/test_temp_pool && ./build/test_temp_pool` |
| 崩溃定位 | `gdb -batch -ex run -ex bt --args test/build/test_temp_pool` |
| 服务端编译（不改动仓库） | `make TARGET=/tmp/cweb_orig SRCS="$(find . -name '*.c' -not -path './test/*' -printf '%P\n')"` |
| 原始日志 | 原版基线：`/tmp/unit_orig2.log`、`/tmp/e2e_orig.log`、`/tmp/tp_gdb.log`、`/tmp/mk_proof.log`；<br>第 9 章复测：`/tmp/unit_final.log`、`/tmp/unit_run3.log`、`/tmp/e2e_fixed_out2.txt`、`/tmp/demo_query3_out.txt`、`/tmp/diff_final.txt`、`/tmp/warn_after.txt` |
| 复测（第 9 章）用命令 | `cd test && timeout -k 5 30 stdbuf -o0 -e0 ./build/<用例>`；套件：`make -C test run`（已内置 30s/用例超时与 HANG 判定） |

> 报告结束。第 1~8 章与附录 A/B/C 的全部结论基于对 `314174f` **未修改源码**的实测，未对被测代码做任何改动；**第 9 章**为使用者自行修改 4 个文件后的复测记录（同样只读测试，未改动被测源码），其原始日志见 9.8。
