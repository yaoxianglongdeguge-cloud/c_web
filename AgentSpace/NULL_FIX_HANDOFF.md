# 三处 NULL 解引用修复说明（供你手工改源文件）

> 本文件由 Cline 生成，**没有改动你的任何源文件**，只是一份「改哪里 / 改成什么 / 为什么 / 怎么验证」的清单。看完可随时删除。
> 对应实测：未修这三处的版本，在 RST 风暴第 1 轮后即 **段错误（核心已转储）**；修好后 6 轮 × 80 次全过、进程存活、c50 3.4 万、空闲后延迟 1.7ms。

## 0. 共同根因：`FdTableFind` 查不到就把出参置 NULL

`FdConnectManage/FdConnectTable/FdConnectTable.c:41`

```c
int FdTableFind(Fd_Table* h,int fd,Fd_Entry** ptr)
{
    *ptr=NULL;                                    /* ← 一进来就把出参保底清空 */
    if(h==NULL||h->Elem==NULL) return 0;
    ...
    if(e==1&&p->next!=NULL&&p->next->fd==fd) {*ptr=p->next; return 1;}   /* 命中 → 1 */
    else { return -1; }                           /* 未命中 → -1，ptr 仍是 NULL */
}
```

**全仓库只有 4 处调用**（`grep -rn 'FdTableFind('`）：

| # | 位置 | 状态 |
|---|---|---|
| 1 | `FdConnectOrClose/FdConnectOrClose.c:51` | ❌ 未校验 → **本文件 §1** |
| 2 | `ResponseSend/ResponseSendMain.c:33` | ❌ 条件不完整 → **§2** |
| 3 | `ResponseSend/ResponseSendMain.c:89` | ❌ 未校验 → **§3** |
| 4 | `RequestCome/RequestComeMain/RequestComeMain.c:8` | ✅ 你已自行加了 `if(fd_ob==NULL) return 0;` |

**为什么 RST 风暴必然踩到**：`FdClose()` 的调用点全部在接收线程这一条链上
（`ResponseSendMain.c:44/99/105/138/139`、`Timer/Timer.c:91`、`RequestComeMain.c:17/22/50/64/89`）。
同一个 fd 会在**同一批 `events[]` 里被处理两次**（EPIPE/对端关闭先关一次；本批后续迭代或 EPOLLOUT 续写路径再进来一次），第二次 `FdTableFind` 必然 miss。

## 1. `FdConnectOrClose/FdConnectOrClose.c` → `FdClose()` 第 50~56 行（最常崩，先改这里）

**现状**
```c
50    Fd_Entry* fd_ob=NULL;
51    FdTableFind(w->fd_table,client_fd,&fd_ob);
52    
53    //回收send池
54    SendPointerPoolDestory(fd_ob->send_pointer_pool,w->store_area);   // ← 崩点①（fd_ob 为 NULL）
55    //回收http暂存区
56    RequestTempPoolDestory(fd_ob->request_temp_pool,w->store_area);   // ← 崩点②
```

**改成（新增 1 行，插在第 51 行之后）**
```c
    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,client_fd,&fd_ob);
    if(fd_ob==NULL) return 1;   //该 fd 已不在连接表：重复关闭，直接返回（⚠️ 必须是 == 不是 =）
```

**为什么这样改是安全的**
- `FdConnect()`（同文件 :11~38）是 `accept → EpollReadMount → TimerAlloc → FdTableInsert`，**中途没有任何 `FdClose` 调用**。所以「查不到」只可能是**已经被关过一次了**（或表被清空），不存在「新连接还没来得及插入就被要求关闭」的正常场景。
- 这种重复调用下，继续往下走会执行 `FdTableDelete / TimerFree / EpollDelete / close(client_fd)`——这些操作作用在**已经关闭、甚至已被新 `accept()` 复用的同一个 fd 号**上，后果比崩溃更隐蔽：可能删掉/关掉别人的连接。
- 所以**提前 return、什么都不做**才是正解。返回值给 `1` 最省事：`FdConnectManage/Timer/Timer.c:91` 会用到它——
  ```c
  int e1=FdClose(w,t->q->queue[1].fd,408);
  if(e1!=1) return -1;            // 返回 0 会让这一轮超时回收提前退出（不崩，只是剩下的超时连接延到下一轮）
  ```
  给 `0` 也能跑（其余调用点都不检查返回值），但 `1` 语义更顺、不打断超时扫描循环。
- ⚠️ 请**不要**写成「跳过池回收但仍然 `close(client_fd)`」——重复 close 的坑只是换个地方爆（fd 号复用）。

## 2. `ResponseSend/ResponseSendMain.c` → `GetAndJudge()` 第 32~42 行

**现状**
```c
32        Fd_Entry* fd_ob=NULL;
33        int e1=FdTableFind(w->fd_table,s.fd,&fd_ob);
34                
35        //如果该连接不存在，或者获取连接失败
36        if(e1!=1&&s.m_queue!=NULL){
37            WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr);
38            continue;
39        }
40
41        //如果连接存在但是没有send暂存池
42        if(fd_ob->send_pointer_pool->store==NULL){     // ← 崩点（fd_ob 为 NULL）
```

**改成（改 3 行，注意 `&&` → `||`、`m_queue` 判断挪进里面）**
```c
        //连接不存在（或取不到连接对象）：先把内存引用还给 Worker 内存队列，再跳过
        if(e1!=1||fd_ob==NULL){
            if(s.m_queue!=NULL) WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr);
            continue;
        }
```

**为什么**
- 原条件 `e1!=1 && s.m_queue!=NULL` 只在「恰好好有内存队列」时才跳过；而 **`m_queue==NULL` 的条目是合法存在的**（静态文件 / 错误响应这类响应不走 Worker 内存队列），于是在 miss 时会穿过 `if` 直接落到第 42 行 `fd_ob->send_pointer_pool` → NULL 解引用崩溃。
- 改成 `e1!=1 || fd_ob==NULL`：查不到连接**一律**跳过。同时把 `m_queue!=NULL` 的判断挪进分支体内做 `WorkerQueuePush(s.m_queue, s.size_resp, s.char_ptr)`，把 `char_ptr` 指向的内存块**归还给 Worker 内存队列**，避免内存泄漏（这正是原来那个 `&&` 的意图，只是条件不完整）。
- 语义不变：`*send_info=1` 已在第 31 行置位，`continue` 后本轮不再取新条目、也不发包，与原来"跳过"的行为一致。

## 3. `ResponseSend/ResponseSendMain.c` → `ResponseSendMain()` 第 88~90 行

**现状**
```c
88    Fd_Entry* fd_ob=NULL;
89    FdTableFind(w->fd_table,send_fd,&fd_ob);
90    int next=fd_ob->ser_nex_send%fd_ob->send_pointer_pool->blocknum;   // ← 崩点（fd_ob 为 NULL）
```

**改成（新增 1 行，插在第 89 行之后）**
```c
    Fd_Entry* fd_ob=NULL;
    FdTableFind(w->fd_table,send_fd,&fd_ob);
    if(fd_ob==NULL) return 1;   //连接已不在表中：没有待发包可发
    int next=fd_ob->ser_nex_send%fd_ob->send_pointer_pool->blocknum;
```

**为什么**
- `send_fd` 有两个来源（见同文件 :73~85）：
  1. `fd == -2` 时由 `GetAndJudge()` 内部查出并回填 —— 正常存在；
  2. **`fd != -2` 时直接 `send_fd = fd`** —— 这是 EPOLLOUT「续写」路径（`Threads/ReceiverThread.c:100`），**沿用的是同一批 `events[]` 里的 fd 号**。
- RST 风暴下这一批事件可能上千个，其中某个 fd 已被本批更早的迭代（EPIPE :138、写 0 :139/:105、408 超时 `Timer.c:91`、对端关闭 `RequestComeMain.c:22/50`）关掉并从表里摘除 ⇒ 这次 `FdTableFind` 必然 miss ⇒ `fd_ob` 为 NULL。
- 返回 `1` 而不是 `0`：函数返回值只被 `ReceiverThread.c:70` 用来判断"是否还有包要发"，`1` = 「本轮到此为止，去处理下一个事件」，与第 85 行 `if(send_fd==-2) return 1;` 的语义一致。
- 此处**无内存需要归还**：该 fd 的 send 指针池已在 `FdClose()` 里被 `SendPointerPoolDestory()` 回收。

## 4. 汇总 diff（也可以直接 `patch -p1` 用；与我给你的 `~/cweb_audit_backup/cweb_crash_fix_now.patch` 完全一致）

```diff
--- a/FdConnectOrClose/FdConnectOrClose.c
+++ b/FdConnectOrClose/FdConnectOrClose.c
@@ -49,6 +49,7 @@
 
     Fd_Entry* fd_ob=NULL;
     FdTableFind(w->fd_table,client_fd,&fd_ob);
+    if(fd_ob==NULL) return 0;   //fd 已不在连接表中：无池可回收，直接返回（原为解引用 NULL）
     
     //回收send池
     SendPointerPoolDestory(fd_ob->send_pointer_pool,w->store_area);
--- a/ResponseSend/ResponseSendMain.c
+++ b/ResponseSend/ResponseSendMain.c
@@ -32,9 +32,9 @@
         Fd_Entry* fd_ob=NULL;
         int e1=FdTableFind(w->fd_table,s.fd,&fd_ob);
                 
-        //如果该连接不存在，或者获取连接失败
-        if(e1!=1&&s.m_queue!=NULL){
-            WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr);
+        //如果该连接不存在（或取不到连接对象），先把内存引用还给 Worker 内存队列再跳过
+        if(e1!=1||fd_ob==NULL){
+            if(s.m_queue!=NULL) WorkerQueuePush(s.m_queue,s.size_resp,s.char_ptr);
             continue;
         }
 
@@ -87,6 +87,7 @@
     //开始发包
     Fd_Entry* fd_ob=NULL;
     FdTableFind(w->fd_table,send_fd,&fd_ob);
+    if(fd_ob==NULL) return 1;   //连接已不在表中：无待发包可发（原为解引用 NULL）
     int next=fd_ob->ser_nex_send%fd_ob->send_pointer_pool->blocknum;
```

- 大小：**2 文件 / 33 行**，md5 `659b55e094523c75634445666c6d6eca`
- 补丁里第 1 处写的是 `return 0`（可用）；手改时写 `return 1` 也可以、更顺（理由见 §1）。**两者都不崩，关键是 `==` 绝不能写成 `=`**。
- 性能代价：**0**（只加 NULL 分支，不进热路径；A/B 实测 c50 34,643 → 34,964、延迟 1.6 → 1.7ms，均在误差内）
- 编译：应用后 `make -j4` **exit=0**（已在你的树副本上验证过两次）

## 5. 改完怎么验证

```bash
cd /home/yaoxianglongde/c_code/c_web
make -j4                       # 必须重编：仓库 ./main 是旧二进制，直接跑 ./main 测的是老代码
bash test/run_tests.sh         # 期望 exit=0（含 taskqueue_race 23 PASS / 0 FAIL）
```

想先在不影响你仓库的副本上试：

```bash
rm -rf /tmp/try && cp -r /home/yaoxianglongde/c_code/c_web /tmp/try
cd /tmp/try && patch -p1 --dry-run < ~/cweb_audit_backup/cweb_crash_fix_now.patch   # 只看能不能打上
cd /tmp/try && rm -f main && find /tmp/try -name '*.o' -delete && make -j4           # 副本上重编
bash ~/cweb_audit_backup/storm2.sh /tmp/try 副本 6 40                                 # 风暴复验
```

`storm2.sh` 是**安全版**：只 `kill` 它自己启动的那个 PID，**不会 `pkill`**、不影响你其它终端/进程。

**期望结果**（与我这轮实测一致）

| 指标 | 期望 |
|---|---|
| 启动 | 正常监听，空闲 CPU **3~5%**（RSS ≈ 3.3MB） |
| 空闲 12s 后 3 个请求 | **≈ 1.6 ~ 2.2 ms**（若仍是 50ms 说明 `epoll_wait` 的 `1` 没生效） |
| wrk `-c50` | **≈ 3.4 万** requests/sec |
| wrk `-c200` | **≈ 5.5 万** requests/sec |
| RST 风暴 6 轮 × 80（`/hello` + `/download/10mb`，`SO_LINGER` 立刻断） | 6 轮全过、**存活=yes**、风暴后 c50 ≈ 3.3 万 |
| 风暴后 RSS | 3300 → 约 3700~3800 KB（不持续增长） |

## 6. 复核清单（改完自查）

| 检查 | 命令 | 期望 |
|---|---|---|
| 三处都改了（**注意 `==` 两边可能有空格**） | `grep -nE 'fd_ob *== *NULL' FdConnectOrClose/FdConnectOrClose.c ResponseSend/ResponseSendMain.c` | 命中 **3** 行 |
| **绝不能写成赋值 `=`** | `grep -nE 'if\(fd_ob *= *NULL' FdConnectOrClose/FdConnectOrClose.c ResponseSend/ResponseSendMain.c` | **0 行**（有输出就是 bug，见 §7） |
| 没有别的漏网调用点 | `grep -rn 'FdTableFind(' --include=*.c .` | 只有 4 处（3 处已加校验 + `RequestComeMain.c:8` 你已修） |
| `&&` 确实改成 `\|\|` | `sed -n '36p' ResponseSend/ResponseSendMain.c` | `if(e1!=1\|\|fd_ob==NULL){` |
| 无新告警 | `make -j4 2>&1 \| grep -i warning` | 不应出现 `-Wparentheses` |
| **真实连接冒烟（必做）** | 起服务后 `curl -s http://127.0.0.1:5343/hello >/dev/null`，再 `curl` 两次 | 服务不退出（单测查不出这个 bug，见 §7） |

### §7 实战踩坑记录（2026-10-15，务必看）

你第一版把第 ① 处写成了：

```c
if(fd_ob = NULL) return 1;        // ← 赋值！不是比较
```

后果**比原来的 bug 更严重**：

```c
if(fd_ob = NULL)      // ① 把 fd_ob 赋成 NULL；② 表达式值为假 ⇒ return 永不执行
                      // ③ 于是必然落到下一行 → 每次 FdClose 100% 崩
SendPointerPoolDestory(fd_ob->send_pointer_pool, ...);   // fd_ob 必为 NULL
```

实测证据（`/tmp/verify_v3` = 你当时的树，全量重编）：

```
[C_你改后] 空闲12s后3请求: 0.003042s 1.156568s 0.000107s     ← 第 2 个请求就崩了
[C_你改后] c200= / c50=（空）
段错误（核心已转储）
```

**连风暴都不用**——第一次连接关闭（curl 收完包断开 → `RequestComeMain.c:22` `FdClose(...,200)`）就崩。编译期其实已经点名了（`-Wall`）：

```
FdConnectOrClose/FdConnectOrClose.c:52:8: warning: suggest parentheses around assignment used as truth value [-Wparentheses]
```

**改对之后**（`if(fd_ob==NULL) return 1;`，`/tmp/verify_v4`，同样全量重编）：

| 指标 | 结果 |
|---|---|
| 空闲 CPU / RSS | 3% / 3300 KB |
| 空闲 12s 后 3 请求 | **1.52 / 1.80 / 1.51 ms** |
| c200 | **49,239** |
| RST 风暴 6 × 80 | 6 轮全过（每轮 0.04~0.07s）、**存活=yes** |
| 风暴后 | c50 28,386，RSS 3300 → 3804 KB |
| `test/run_tests.sh` | **exit=0**（`taskqueue_race` 23 PASS） |

⚠️ **重要教训**：`test/run_tests.sh` 在你**带笔误**的版本上同样 **exit=0 全通过**——单测不覆盖 `FdClose`。所以判断这类 bug **必须用真实连接**（curl 一次就现形），别只看单测。

> 注：②（`GetAndJudge` 的 `&&`→`||`）和 ③（`ResponseSendMain.c:92 if(fd_ob == NULL) return 1;`）你改得**完全正确**。

### §8 结项：三处全部就位，验证通过（2026-10-15 收尾实测）

你的最终代码（`FdConnectOrClose.c:52  if(fd_ob == NULL) return 1;`、`ResponseSendMain.c:36  if(e1!=1||fd_ob==NULL)`、`ResponseSendMain.c:92  if(fd_ob == NULL) return 1;`）在**你仓库的副本**上全量重编 + 压测 + 回归：

| 指标 | 结果 |
|---|---|
| 编译 | `make -j4` **exit=0**，**`-Wparentheses` 告警消失** |
| 空闲 CPU / RSS | 4% / 3304 KB |
| 空闲 12s 后 3 请求 | **2.77 / 1.57 / 1.63 ms** |
| wrk c50 / c200 | **34,640 / 56,261** |
| **RST 风暴 6 × 80** | 6 轮全过（0.03s/轮）、**存活=yes** |
| 风暴后 | c50 **34,816**，RSS 3304 → 3800 KB（不增长） |
| `test/run_tests.sh` | **exit=0**（taskqueue_race 23 PASS / 0 FAIL） |

**完整对照时间线（同一套风暴脚本）**

| 版本 | 结果 |
|---|---|
| `epoll=50` + 无 NULL 校验 | RST 首轮 → **段错误** |
| `epoll=1` + 无 NULL 校验 | 仍 → **段错误**（第 1 轮后） |
| `epoll=1` + guard 写成 `=` | **curl 一次就崩**（更糟，见 §7） |
| `epoll=1` + guard 写对（`==`） | **风暴全过、存活、c50 ≈3.5 万、延迟 1.6ms** ✅ |

**⚠️ 别忘了重编你仓库里的二进制**：`main` 的时间戳仍是 09:01，而源码已改到 17:08——直接跑 `./main` 测的还是旧代码：

```bash
cd /home/yaoxianglongde/c_code/c_web && make -j4
```

**剩余两个无害的既有告警（可选清理，非本次引入）**

- `FdConnectOrClose/FdConnectOrClose.c:46: unused variable 'n'` —— `FdClose()` 里 `write(client_fd, re, len)` 的返回值没被使用（也许顺带该处理一下写失败）。
- `Threads/ReceiverThread.c:70: unused variable 'e2'` —— 你改 epoll 那处带出来的。



> 备注：`RequestCome/RequestComeMain/RequestComeMain.c:8` 那处**你已自行加过 `if(fd_ob==NULL) return 0;`**，故不在本清单内。
> 相关背景（为什么 50ms 长尾、为什么睡眠参数是对的）：见 `~/cweb_audit_backup/CWEB_BOUNDARY_AUDIT.md`（266 行，§6.7 / §6.8 / §6.10）。



