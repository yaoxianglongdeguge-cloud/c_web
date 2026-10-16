# c_web 两个 P0 问题报告（大白话版）

> 本报告由 Cline 生成，**没有改动任何 `.c/.h` 源码**，所有数字都来自副本上的实测。
> 报告里出现的"副本"= 把你的源码复制到 `/tmp` 里加统计/加补丁后编译的版本，用来做对照实验。

**怎么读这份报告**（不想全看就按这个顺序翻）：

- 只想知道"严重不严重" → 看 **第 0 节**（一张表说完）
- 想自己动手验证一遍 → 看 **2.2 / 3.2 节**（命令可直接复制粘贴）
- 想知道到底为什么 → 看 **2.3 / 3.3 节**（有打饭/格子比喻 + 具体代码位置）
- 想动手修 → 看 **2.5 / 3.4 节**（给了"改动前 → 改动后"的代码），改完按 **2.6 / 3.5 节** 验收
- 现在仓库根本编不过 → 直接跳到 **第 5 节**

**目录**： 0 结论 · 1 名词解释 · **2 P0-1**（2.1 症状 / 2.2 复现步骤 / 2.3 原因 / 2.4 证据 / 2.5 修法 / 2.6 修复效果）· **3 P0-2**（3.1 症状 / 3.2 复现步骤 / 3.3 原因A+原因B / 3.4 修法 / 3.5 验收）· 4 动手顺序 · 5 先让仓库能编译 · 6 证据文件位置

---


| 编号 | 问题 | 一句话说明 | 严重度 | 触发条件 |
|---|---|---|---|---|
| **P0-1** | 错误响应漏内存 | 每发一次错误响应（404/405…）就**漏掉一块内存**；漏满 120 次后服务器的内存池空了，于是**所有请求（连正常的 `/hello`）都永久返回 503** | 🔴 致命，30 秒就能复现 | 只要有人请求不存在的地址（扫描器、爬虫、打错的链接都会） |
| **P0-2** | 请求被"卡"住 | 客户端一次发很多请求（流水线）时，接收线程半路停下后**没人回来继续处理**，已经收到的那些请求**永远不回响应**，但连接不断开 → 客户端一直等到超时 | 🔴 功能缺陷，偶发丢 2%~15% | 多个连接同时一次性发很多请求 |

---

## 1. 三个名词先用大白话解释（不然下面看不懂）

- **内存池**：程序启动时先"买好一箱格子"，比如 1KB 的 120 格、2KB 的 120 格、…、64KB 的 120 格。
  要用内存 = 从箱子里**取一格**（alloc），用完 = **还回去**（free）。
- **池满降级 503**：箱子里这类格子取空了 → 框架就对客户端回"503 服务不可用"（这是你自己设计的行为）。
- **错误响应**：不在路由表里的 URL、方法不对等 → 框架回 404/405… 这些就是"错误响应"。

> 一句话：**格子被借走后没还，箱子空了，后面所有请求都借不到格子 → 全站 503。这就是 P0-1。**

---

## 2. P0-1：错误响应漏内存 → 全站永久 503

### 2.1 你会看到什么（症状）

只反复访问一个不存在的地址 `/nope`：

| 时间 | 内存占用(RSS) | 请求 `/nope` | 请求 `/hello`（正常路由） |
|---|---|---|---|
| 刚启动 | 3.3 MB | 404（正常） | 200（正常） |
| 压测 15 秒后 | 12.9 MB | **503 不可用** | **503 不可用** |
| 压测 30 秒后 | 21.1 MB | 503 | 503 |

**注意第二列**：一旦变成 503，**再也回不来**（池子不会回收、不会扩容），必须重启进程。
而且**正常路由 `/hello` 也被拖下水**，这就是"全站挂掉"。

### 2.2 你自己怎么看到（复制就能跑）

```bash
# 1) 先让仓库能编出来（见第 5 节，缺 AgentSpace/foo.h）
cd <你的副本目录>/AgentSpace
../main &                       # 启动服务器（监听 127.0.0.1:5343）
sleep 1

# 2) 先看正常
curl -s -o /dev/null -w 'nope=%{http_code}\n' http://127.0.0.1:5343/nope    # nope=404
curl -s -o /dev/null -w 'hello=%{http_code}\n' http://127.0.0.1:5343/hello  # hello=200

# 3) 只打 404，打 30 秒
wrk -t2 -c50 -d30s http://127.0.0.1:5343/nope

# 4) 再看
curl -s -o /dev/null -w 'nope=%{http_code}\n' http://127.0.0.1:5343/nope    # nope=503 ← 挂了
curl -s -o /dev/null -w 'hello=%{http_code}\n' http://127.0.0.1:5343/hello  # hello=503 ← 正常的也挂了
ps -o rss= -p <服务器pid>      # 内存占用会涨到十几~二十几 MB
```

对照组（说明**只有错误响应这条路有问题**）：

| 负载（都跑 30 秒） | 内存变化 | 结论 |
|---|---|---|
| `GET /hello`（一直复用连接） | 3324 → 3392 kB | 几乎不涨 ✅ |
| `GET /index.html` （静态文件） | 3328 → 3392 kB | 几乎不涨 ✅ |
| `GET /hello` + `Connection: close`（每请求关连接） | 3328 → 3392 kB | 几乎不涨 ✅（所以**不是**"关连接"导致的） |
| **`GET /nope`（404）** | **3324 → 21904 kB** | 每 10 秒涨 6 MB ❌ |

### 2.3 为什么会这样（一次 404 的"内存旅行"）

用"借格子"来说：404 这条路**借了一格却没还**。

具体到代码，一次 404 走了这 4 步：

```
① Threads/WorkerThread.c  包好响应：PackTotal() 里"先借一格"用来装要发的数据
      MemoryPoolAlloc(profes->txt_pool, *allocsize/1024+1, &ptr2, &notfull2);
   ▶ 问题：即使是 404（并不需要装数据），也照样借了

② Threads/WorkerThread.c  PackBackHead()：发现是错误响应，走"错误分支"
      它把 body、headers 还掉，然后就 return 了
   ▶ 问题：它**根本不碰**①里借的那一格 → 这格再也没人还

③ Threads/WorkerThread.c  把结果交给接收线程时，那个"内存指针"传的是 NULL
      ReceiverQueuePush(..., NULL /*m_queue*/, ..., NULL /*char_ptr*/, ...)
   ▶ 这条"让 worker 事后还格子"的正常回收通道也断了

④ ResponseSend/ResponseSendMain.c  接收线程收到"错误码"后
      自己用 BuildErrorResponse() 写一份 404 文本发回去，然后关连接
   ▶ 客户端看到的 404 完全正常 —— 所以**这个问题被完全掩盖**，不看内存根本发现不了
```

**雪上加霜的是格子数很少**：工作线程的箱子 `MemoryPoolInit(&txt_pool, 7, 7, 200, 120)`，
1KB 这类只有 **120 格**。所以：

```
120 次错误响应  →  1KB 这类格子全部被漏光  →  之后连正常响应也借不到格子
              →  框架按设计降级为 503  →  所有请求都 503，且永不恢复
```

### 2.4 证据（不是猜的，是打出来的）

我给内存池加了临时统计（只加在 `/tmp/instr` 副本里），跑 15 秒 404 后各箱子的"借/还"差额：

| 箱子（按每类格数识别） | 借出次数 | 归还次数 | **净漏格子** |
|---|---|---|---|
| 接收线程 http_pool（每类 400 格） | 266702 | 266677 | 25 |
| **工作线程1 的 txt_pool（每类 120 格）** | 270262 | 135132 | **135130** ← 几乎每请求漏一格 |
| 工作线程1 的 http_pool（每类 10 格） | 135131 | 135131 | 0 ✅ |
| **工作线程2 的 txt_pool** | 263148 | 131573 | **131575** ← 同样 |
| 工作线程2 的 http_pool | 131574 | 131574 | 0 ✅ |
| store_area（连接相关） | 533425 | 533425 | 0 ✅ |

耗尽那一瞬间服务器自己的日志（我加的探针）：

```
ALLOCFAIL pool=0x...f1e0  size=1KB  class=0  strips=120  inuse=120  freeTop=0
```

读法：**这一类共 120 格，全部在用（inuse=120），空闲格为 0（freeTop=0）** → 从此借不到。
另外 `FREEFAIL`（"还格子时尺寸对不上被拒"）次数为 **0** → 说明不是"还错了"，而是**压根没还**；
glibc malloc 用量一直稳定在 ~725KB → 说明**不是普通 malloc 泄漏**，就是内存池格子被漏。

### 2.5 怎么修（改动很小）

**思路**：错误响应根本不需要①那一格（错误文本由接收线程统一生成），所以**只在正常响应时才借**。

打开 `Threads/WorkerThread.c` 的 `PackTotal()`，原来是：

```c
    *allocsize=strlen(Rsp->headers->begin)+Rsp->body_size+17;
    void* ptr2=NULL;
    int notfull2=0;
    MemoryPoolAlloc(profes->txt_pool,*allocsize/1024+1,&ptr2,&notfull2);

    if(notfull2==0)  Rsp->error_reason=503;
    else  *C = ptr2;

    PackBackHead(C,Rsp);
```

改成：

```c
    /* 【修复】错误响应不需要"打包块"：原来无条件分配，而 PackBackHead 的错误分支
       不释放它，WorkerMain 又用 NULL 回传 ⇒ 每 1 次错误响应漏 1 格。
       现在只在 200 时分配；错误响应仍然调用 PackBackHead 以回收 body/headers 并关文件 fd。 */
    if(Rsp->error_reason==200){
        *allocsize=strlen(Rsp->headers->begin)+Rsp->body_size+17;
        void* ptr2=NULL;
        int notfull2=0;
        MemoryPoolAlloc(profes->txt_pool,*allocsize/1024+1,&ptr2,&notfull2);

        if(notfull2==0)  Rsp->error_reason=503;
        else  *C = ptr2;
    }else{
        *allocsize=0;
    }

    PackBackHead(C,Rsp);     /* 错误分支只做回收，不碰 *C，所以这里不会用到未初始化的指针 */
```

> 顺手建议（同一函数）：`strlen(Rsp->headers->begin)` 前面加一句
> `if(Rsp->headers==NULL){ Rsp->error_reason=503; ... }`。
> 因为 `RspHeaderAlloc()` 分配失败时只设了 503、`headers` 会是 NULL，这里会直接崩。

### 2.6 修完的效果（我在副本上实测过）

同样跑 60 秒 `/nope`（约 60 万次请求）：

| 时间 | 内存占用 | 请求 `/nope` | 请求 `/hello` |
|---|---|---|---|
| 刚启动 | 3.3 MB | 404 ✅ | 200 ✅ |
| +20 秒 | 13.5 MB | **404 ✅** | **200 ✅** |
| +40 秒 | 20.3 MB | **404 ✅** | **200 ✅** |
| +60 秒 | 24.4 MB | **404 ✅** | **200 ✅** |

**致命症状消失**：不再出现"全站 503"。
**但没全好**：内存还是从 3.3MB 涨到 24.4MB，吞吐也从 14.7k 掉到 5.8k rps。
这次不是"格子漏了"（上面表格里借/还已对齐），更像是**箱子里的页被逐步触碰**+高连接 churn 的开销，
属于**待查的第二条线索**，修完 P0-1 再单独观察即可。

---

## 3. P0-2：请求被"卡"住（偶发丢响应、连接假死）

### 3.1 你会看到什么（症状）

用一个客户端，**一次性**把 120 个请求写进同一个连接（这叫"流水线 / pipelining"），然后读响应：

| 场景（每档都重启一个干净服务器） | 拿回响应 | 结束方式 |
|---|---|---|
| 1 个连接、一次发 100 个 | 100/100 ✅ | 全部读完，连接保持 |
| 1 个连接、一次发 500 个 | 500/500 ✅ | 全部读完，连接保持 |
| **2 个连接、各一次发 60 个（共 120）** | **102/120 ❌ 丢 18（15%）** | 一个连接**读超时**，连接**不断开** |
| 2 个连接、各一次发 60 个（再跑一次） | 120/120 ✅ | ——（说明是**偶发**，看时序） |
| 2 个连接、各一次发 60 个（第三次） | 118/120 ❌ 丢 2 | 读超时 |
| 8 个连接、各一次发 25 个（共 200） | 一轮 200/200 ✅、另一轮 176/200 ❌ | 丢的那轮读超时 |

关键特征：**响应缺了一段，但连接还活着**（不是 RST、也不是 503），客户端只能干等到超时。
再加深（1 个连接一次发 800~1000 个）就会直接被 RST，**一个响应都收不到**。

### 3.2 你自己怎么看到（复制就能跑）

```bash
# 服务器先跑起来（同上），然后：
python3 - <<'EOF'
import socket, time, threading
HOST, PORT, NC, DEPTH = "127.0.0.1", 5343, 2, 60     # 改成 1,500 会全部成功（对照）
def one(idx, out):
    s = socket.create_connection((HOST, PORT), timeout=5)
    fmt = "GET /echo?name=%d-%%d HTTP/1.1\r\nHost: c\r\n\r\n" % idx
    s.sendall(b"".join((fmt % i).encode() for i in range(DEPTH)))   # ← 一次性全写进去
    buf, got, t0 = b"", 0, time.time()
    try:
        while got < DEPTH and time.time() - t0 < 5:
            d = s.recv(65536)
            if not d: break
            buf += d
            while True:
                p = buf.find(b"\r\n\r\n")
                if p < 0: break
                cl = None
                for line in buf[:p].split(b"\r\n"):
                    if line.lower().startswith(b"content-length:"): cl = int(line.split(b":")[1])
                if cl is None or len(buf) < p + 4 + cl: break
                buf = buf[p + 4 + cl:]; got += 1
    except Exception as e:
        out.append("异常:" + type(e).__name__); s.close(); return
    out.append(got); s.close()
out = []
ts = [threading.Thread(target=one, args=(i, out)) for i in range(NC)]
t0 = time.time(); [t.start() for t in ts]; [t.join() for t in ts]
print("%d 连接 × 深度 %d ：拿回 %s / %d   用时 %.2fs" % (NC, DEPTH, out, NC*DEPTH, time.time()-t0))
EOF
```
（多跑几次会看到：有时全成功、有时丢十几个 —— 这就是"偶发"。）

### 3.3 为什么会这样（两个结构原因）

#### 原因 A（当前主因）：半路停下后，**没有人"回来继续处理"**

像食堂打饭：接收线程负责"从窗口把菜端进来"（把请求从 socket 读进暂存区），
但**一次能端的份数有上限**——已经端进来、还没送出去的超过 100 份时，它就先停下：

```c
// RequestComeMain.c 开头
if(fd_ob->pack_in_path >= ed_store_blocknum) return 1;   // 在途≥100：直接返回，不管暂存区里剩下的
// 以及投递失败时
int put = ReceiverToWorker(...);
if(put != 1) break;                                      // 跳出，剩下的留在暂存区
```

而接收线程**只在"这个连接又有新数据到达"（EPOLLIN 事件）时才会继续处理它**：

```c
// ReceiverThread.c
if(events[i].events & EPOLLIN) RequestComeMain(handle_fd,w,ed_store_blocknum);
```

问题就来了：客户端"一次性发完再等响应"，那些请求**已经被读进暂存区了**（socket 缓冲区已空），
于是**再也不会有 EPOLLIN**；`pack_in_path` 降下来也**没人回来重跑这个连接**
→ 暂存区里剩下的请求**永远不会被处理**，连接却还开着 → 客户端"缺一段 + 干等超时"。

这也解释了为什么"单个连接一次发 500 个"反而全成功（不会撞上在途上限），
只有"多个连接同时一次性发很多"才容易踩到。

#### 原因 B（潜在的雷，还没爆）：暂存槽只有 1 个，且会被**无条件覆盖**

投递失败时请求会被塞进一个"临时寄存处"，而它**只有一个位置**，写入前也不检查是否已被占用：

```c
// Threads/ReceiverThread.c
int e0=TasKQueuePush(taskQueue,...);
if(e0!=1){
    TempPackPut(w->tempPack,true,fd,h,h_size,error_reason,serial);   // ← 塞进唯一那个槽
    return 0;
}
// TempPackPut 内部：直接赋值，不看 had 是否已经是 true
```

若两个请求在"重试成功之前"先后都失败 → 后一个**把前一个覆盖掉**，后果三连：
1. 那个请求**永远不会被执行、也不会回响应**（客户端只能超时）；
2. 它占用的那块请求内存（接收线程 http_pool 的格子）**永久漏掉**（与 P0-1 同类）；
3. 更麻烦：`RequestComeMain` 里 `fd_ob->ser_fina_send++`（消耗序号）**发生在投递之前**，
   序号记了账但请求丢了 → 发送端按序号顺序等待（`while(store[next].use==1)`），
   **整条连接的后续响应会全部卡死发不出去**。

**实测修正**：我把"真覆盖"和"同一请求重试再入槽"（无害）分开统计，跑了 6 轮：

| 轮次 | 场景 | 客户端结果 | 真覆盖丢失 | 同一请求重试再入槽（无害） |
|---|---|---|---|---|
| 1 | 2×60 | 120/120 ✅ | **0** | 92 |
| 2 | 2×100 | 200/200 ✅ | **0** | 0 |
| 3 | 2×60 | 120/120 ✅ | **0** | 244 |
| 4 | 2×100 | 200/200 ✅ | **0** | 396 |
| 5 | 2×60 | 120/120 ✅ | **0** | 139 |
| 6 | 2×100 | 200/200 ✅ | **0** | 278 |

→ **6 轮里一次"真覆盖"都没抓到**。所以结论是：
**B 是真实存在的隐患（代码摆在那里），但本次丢响应不是它造成的；主因是 A。**
B 要"同一批事件里两个连接同时投递失败"才可能触发（高并发突发时概率才上来），但修起来很便宜，建议一起加固。

### 3.4 怎么修

**修 A（必须做，二选一）**

- **推荐**：让接收线程"记得还有连接要续跑"
  1. `Fd_Entry` 里加一个标记，例如 `int need_recheck;`
  2. `RequestComeMain` 在那两处提前 `return` / `break` 之前把它置 1；
  3. 接收线程每轮循环末尾（`epoll_wait` 之后，包含"超时无事件"那条分支 —— 反正每 1ms 都会醒一次），
     遍历这些连接：只要它的暂存区里还有数据就再跑一次 `RequestComeMain`，跑完清 0。
- **更简单的做法**：`RequestComeMain` 内部改成"在途到 100 就**停止从 socket 读**，但**继续把暂存区里已有的请求处理完**"，
  即把"在途上限"从 `return` 变成"不再读 socket"。

**修 B（加固，建议顺手做）**

1. `TempPackPut` 写入前先判断：`if(tempPack->had){ 记日志/计数 }`；
2. 把"1 个槽"换成"小环形槽"（8~16 个）或链表，清除时按槽位清；
3. 把 `fd_ob->ser_fina_send++` **挪到投递成功之后**（失败时不要消费序号）；
4. 投递连续失败多次时，宁可 `FdClose(w,fd,503)` 明确告诉客户端，也不要"静默缓存后被覆盖"。

### 3.5 修完怎么验收

用 3.2 的脚本跑 **20 轮** `2 连接 × 深度 60` 和 `8 连接 × 深度 25`：
要求每轮"拿回数 == 发送数"、结束方式都是"全部读完(连接保持)"，且服务端日志里 **真覆盖丢失 = 0**。

---

## 4. 建议的动手顺序（按性价比）

| 顺序 | 做什么 | 为什么先做它 |
|---|---|---|
| 1️⃣ | 修 **P0-1**：`PackTotal` 只在 200 时分配打包块 | 改动约 3 行，直接消掉"全站 503"这个致命问题 |
| 2️⃣ | 修 **P0-2 原因 A**：加"续跑"机制 | 消掉"偶发丢请求 / 连接假死"，这是正确性问题 |
| 3️⃣ | 加固 **P0-2 原因 B**：环形槽 + 序号后移 | 很便宜，防高并发突发的雷 |
| 4️⃣ | 顺手加 `headers==NULL` 判断 | 防 64KB 类耗尽时的空指针崩溃 |
| 5️⃣ | 查 P0-1 修复后残留的 RSS 增长（3.3→24MB） | 不再致命，但影响长时间运行 |
| 6️⃣ | 最后再谈性能优化（1.1ms 延迟地板 / memmem / -O2） | 见前一轮性能体检报告 |

---

## 5. 前置：先让你现在的仓库"能编出来"（否则没法复现）

当前仓库编译会失败，原因有三个（都与这两个问题无关，是重构中途的状态）：

| 报错 | 原因 | 处理办法 |
|---|---|---|
| `fatal error: ../AgentSpace/foo.h: 没有那个文件` | `Include/Include.h` 第 2 行引用了 `../AgentSpace/foo.h`，但 `AgentSpace/` 下没有这个文件（对外头文件现在放在 `ToOut/foo.h`） | 把 `ToOut/foo.h` 复制一份到 `AgentSpace/foo.h` |
| `AgentSpace/WebApp/WebApp.c:1: fatal error: foo.h: 没有那个文件` | `main.c` / `WebApp.c` 里写的是 `#include "foo.h"`，编译时找不到 `AgentSpace/` 这个目录 | 根 `Makefile` 的 `CFLAGS` 加 `-IAgentSpace` |
| 可能还有 `multiple definition of 'main'` | 根 `Makefile` 的 `SRCS` 通配会把 `AgentSpace/test/src/*.c`（每个都带 `main`）也编进来 | `SRCS := $(filter-out test/% AgentSpace/test/%,$(SRCS))` |

最快做法（不动任何源码，只补构建配置）：

```bash
cp ToOut/foo.h AgentSpace/foo.h
# 编辑根 Makefile：
#   CFLAGS = -g -O0 -Wall -I. -IAgentSpace
#   SRCS := $(filter-out test/% AgentSpace/test/%,$(SRCS))
make -j4          # 产出 ./main
```

---

## 6. 证据与材料在哪（都在 /tmp，你的源码我一个字没改）

| 路径 | 里面是什么 |
|---|---|
| `/tmp/perf/leak_matrix.txt`、`/tmp/perf/leak_serial.txt` | 各种负载的 RSS 曲线（证明"只有错误响应这条路会漏内存"） |
| `/tmp/instr/` | 加了统计的副本：`MemoryPool.c`（借/还计数 + `ALLOCFAIL` 现场）、`ReceiverThread.c`（真覆盖/重试计数）、`analyze.py`、`nope.log` |
| `/tmp/fix/` | **P0-1 的最小修复** + 验证脚本 `verify_fix.sh`（60 秒 404 压测不再 503） |
| `/tmp/cur/` | 你现在源码的可构建副本（补了 `AgentSpace/foo.h` 与 `-IAgentSpace`） |
| `/tmp/perf/stability_report.txt` | 混合负载退化曲线（503 占比从 7.7% 涨到 50%、RSS 3.3→33MB） |
| `/tmp/perf/diag_report.txt` | 每请求 ~1.1ms 延迟地板、线程负载分布（另一个大问题的原始数据） |

> 报告结束。需要我把 **P0-1 补丁 + P0-2 的 A、B 两处修法**直接写成能贴进源码的完整 diff（或直接在副本里改好、跑一遍给你看结果）吗？

---

## 7. 修复记录（P0-1 已落地并通过验收）

### 7.1 改了哪两个文件、改了什么

| 文件 | 改动 | 作用 |
|---|---|---|
| `Threads/WorkerThread.c` | ① 修正笔误 `Rsp->headers` → `Rsp.headers`（**原来是编译不过的**）<br>② 错误响应分支补上回收：`{ char* C=NULL; PackBackHead(&C,&Rsp); }`<br>③ `RspHeaderAlloc` 池满时置 `headers=NULL` 并 `return`（原来会往 NULL 写而**段错误**） | 消除"每个错误响应漏 1 块"⇒ 不再全站 503、不再崩溃 |
| `FdConnectResource/RequestTempPool/RequestTempPool.c` | `RequestTempPoolDestory()` 里补 `free(h->state_machine)` | 消除"每个新连接漏 12 字节"⇒ RSS 不再持续上涨 |

> 说明：`WorkerThread.c` 里"把 `PackTotal` 挪进 `else`（200 分支）"这一步是**你先做的**，方向正确，我保留了它；
> 但它同时把 `PackTotal → PackBackHead` 里原本负责的"回收 headers/body + 关文件 fd"也一起绕过了，
> 这就是第 ② 步要补回来的原因（否则泄漏从 1KB 变成 64KB，而且 120 次错误响应后直接**段错误**）。

### 7.2 为什么还要改 `RequestTempPool.c`（valgrind 实证）

`StateMachineInit()` 在 `RequestTempPoolInit()` 里 `malloc` 了一个状态机，但**全代码没有任何地方 `free` 它**。
valgrind 报告（150 个 404 + 15 个 hello = 166 个请求）：

```
1,992 bytes in 166 blocks are definitely lost
    at malloc
    by StateMachineInit        (StateMachine.c:5)
    by RequestTempPoolInit     (RequestTempPool.c:14)
    by FdTableInsert           (FdConnectTable.c:77)     ← 每个新连接一次
    by FdConnect               (FdConnectOrClose.c:31)
```

补上 `free` 后同一实验：**definitely lost: 0 bytes in 0 blocks** ✅

### 7.3 验收数据（同机、同副本、同一套命令）

| 项目 | 修复前 | 修复后 |
|---|---|---|
| 30 秒 `/nope` 压测后的 `/hello` | **503（全站不可用）** | **200 ✅** |
| 60 秒 `/nope` 压测的 RSS | 3.3MB → **41MB**（线性涨） | **稳定 3384 kB ✅** |
| `txt_pool` 净漏块（15 秒 404） | 135130 + 131575 | **0 ✅** |
| glibc `malloc` 用量（uordblks） | 2.1MB → 14.9MB | 平稳 ✅ |
| valgrind definitely lost | 1992 B / 166 块 | **0 B / 0 块 ✅** |
| 网站功能测试（27 项） | —— | **PASS 27 / FAIL 0 ✅** |
| `/nope` 吞吐（60 秒内） | 掉到 5.8k rps | **17~18k rps 稳定 ✅** |
| 服务端 CPU/请求（c50 404） | —— | 约 13~14 µs |

### 7.4 还没解决（下一步）

1. **P0-2 未修**：多连接一次性流水线仍会偶发丢响应 / 连接假死（见第 3 节）。
2. **每请求 ~1.1ms 延迟地板**：接收线程只靠 `epoll_wait(...,1)` 的 1ms tick 才发响应（见性能体检报告）。
3. 接收线程 `http_pool` 上仍有零星漏块（60 秒/百万请求级：83 块），与 P0-2 的 `tempPack` 覆盖同源；
   修掉 P0-2 的原因 A/B 后应一并消失。

### 7.5 注意：仓库要能编译还需补构建配置（与本次修复无关）

```bash
cp ToOut/foo.h AgentSpace/foo.h      # Include/Include.h 引用了 ../AgentSpace/foo.h
# 根 Makefile：
#   CFLAGS = -g -O0 -Wall -I. -IAgentSpace
#   SRCS := $(filter-out test/% AgentSpace/test/%,$(SRCS))
make -j4
```



