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
    if(fd_ob==NULL) return 0;   //该 fd 已不在连接表：本次是重复关闭，直接返回
```

**为什么这样改是安全的**
- `FdConnect()`（同文件 :11~38）是 `accept → EpollReadMount → TimerAlloc → FdTableInsert`，**中途没有任何 `FdClose` 调用**。所以「查不到」只可能是**已经被关过一次了**（或表被清空），不存在「新连接还没来得及插入就被要求关闭」的正常场景。
- 这种重复调用下，继续往下走会执行 `FdTableDelete / TimerFree / EpollDelete / close(client_fd)`——这些操作作用在**已经关闭、甚至已被新 `accept()` 复用的同一个 fd 号**上，后果比崩溃更隐蔽：可能删掉/关掉别人的连接。
- 所以**提前 return、什么都不做**才是正解；返回 `0` 表示「本次没有真正关闭任何连接」。调用方目前都不检查返回值（`grep -rn 'FdClose('` 可见），不影响既有逻辑。
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
| 三处都改了 | `grep -n 'fd_ob==NULL' FdConnectOrClose/FdConnectOrClose.c ResponseSend/ResponseSendMain.c` | 命中 **3** 行 |
| 没有别的漏网调用点 | `grep -rn 'FdTableFind(' --include=*.c .` | 只有 4 处（3 处已加校验 + `RequestComeMain.c:8` 你已修） |
| `&&` 确实改成 `\|\|` | `sed -n '36p' ResponseSend/ResponseSendMain.c` | `if(e1!=1\|\|fd_ob==NULL){` |
| 无新告警 | `make -j4 2>&1 \| grep -i warning` | 与改前一致（这三处不引入新告警） |

> 备注：`RequestCome/RequestComeMain/RequestComeMain.c:8` 那处**你已自行加过 `if(fd_ob==NULL) return 0;`**，故不在本清单内。
> 相关背景（为什么 50ms 长尾、为什么睡眠参数是对的）：见 `~/cweb_audit_backup/CWEB_BOUNDARY_AUDIT.md`（266 行，§6.7 / §6.8 / §6.10）。



