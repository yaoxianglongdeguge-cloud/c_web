# c_web 代码体检报告（第二轮：崩溃 · 协议 · 容量）

> 本报告由 Cline 生成。方法：**原始 socket 边界用例**（22 个，每个用例都重启一个干净服务器）+ **valgrind** + **内存池插桩**。
> 源码只动过报告最后"修复清单"里列出的文件；所有数字都是实测。

---

## 0. 本轮新发现总览

| 编号 | 问题 | 严重度 | 触发条件 | 状态 |
|---|---|---|---|---|
| **N1** | 请求体分两次到达 → **野指针 → 服务器进程崩溃** | 🔴 **P0** | POST 头先到、体后到（慢网络/大表单/curl 上传都会） | ✅ **已修已验** |
| **N2** | 解析失败被伪装成 **`200 OK` 空响应**（414/431/400 全部丢失） | 🟠 P1 | URL > 8192、单个头 > 16KB | ✅ **已修已验** |
| **N3** | **>32KB 的请求必然 503**（真实上限 ≈32KB，文档写的是 1MB） | 🟠 P1 | POST 体 ≥ 32KB（32768 就开始失败） | ⏳ 待你决策（第 3 节给 3 个方案） |
| **N4** | 协议语义：不遵守 `Connection: close` / HTTP/1.0、`HEAD`→405、重复 `Content-Length` 与 `chunked` 不过滤 | 🟡 P2 | 代理/客户端兼容性、请求走私风险 | ⏳ 建议（第 4 节） |

> 上一轮已修的两项（错误响应漏内存 → 全站 503；每连接 12 字节 malloc 泄漏）见 `P0_TWO_BUGS_REPORT.md` 第 7 节。
> 本轮另外确认：**fd 无泄漏**（每个用例 5→5）、`/../Makefile` 之类路径穿越返回 404（安全）、超长查询串正常。

---

## 1. N1（P0）：分片请求体让服务器崩溃

### 1.1 现象与复现

用原始 socket，**先把 POST 头发过去，0.3 秒后再发 20 字节请求体**：

```python
import socket, time
s = socket.create_connection(("127.0.0.1", 5343), timeout=3)
s.sendall(b"POST /api/echo HTTP/1.1\r\nHost: c\r\nContent-Length: 20\r\n\r\n"); time.sleep(0.3)
s.sendall(b"0123456789abcdefghij")
print(s.recv(4096))
```

实测结果：

| 用例 | 修复前 | 修复后 |
|---|---|---|
| POST 头+体**一次**发（对照） | 200 ✅ | 200 ✅ |
| POST 头先发、体 **0.3s 后**发 | **无响应 + 服务端关闭 + 进程死了** ❌ | **200 ✅ 进程存活** |
| POST 体**分两段**发（10+10） | **进程死了** ❌ | **200 ✅** |
| 同一连接先 GET、再分片 POST | **进程死了** ❌ | （同一处修复） |
| POST 1MB 分 10 段发 | **进程死了** ❌ | 不再崩（会变成 N3 的 503） |

### 1.2 根因（`RequestCome/RequestIntergrity/RequestIntergrity.c`）

```c
int RequestIntergrityJudge(...)
{
    char* target;                    // ← 未初始化
    if(hs->state_machine->h_rnrn==0){        // 第一次调用：找到 \r\n\r\n
        target=rnrn-1;                        //    给 target 赋值 ✓
        hs->state_machine->h_rnrn=1;          //    置位后，下次这个分支就不进了
    }
    if(hs->state_machine->h_body_length==0){
        char* body_length=strnstr_match(hs->begin,"Content-Length:",check_size);
        ...
        target=target+4+len;                  // 第二次调用时 target 是【野指针】
        if(target<hs->ptr_e){ *fail=1; }      // 于是"完整性判定"用的是垃圾值
    }
    *ptr = target;                            // 把垃圾指针当"请求结尾"返回
```

链路：第一次调用判定"体还没到"（`*fail=-1`）→ 等数据 → 第二次调用 **`h_rnrn` 已经是 1，跳过定位** → `target` 保持未初始化 → 用垃圾指针算出请求长度 → 上游 `RequestComeMain` 拿它算 `h_size` 并 `RequestTempPoolCopy` 做 `memcpy` → **越界写 ⇒ 段错误**（有时是 `MemoryPoolAlloc` 失败 ⇒ 503 断连，所以表现会"时崩时 503"）。

### 1.3 修法（已落地）

```c
    char* target=NULL;               /* ① 先给一个确定值 */

    if(hs->state_machine->h_rnrn==0){
        ... target=rnrn-1;
    }
    else{                            /* ② 上一轮定位过头尾，这里必须重新定位一次 */
        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL){  *fail=-1;  return 0;  }
        target=rnrn-1;
    }
```

> 代价：请求体跨多次读取时，每次多扫一遍 `\r\n\r\n`（本来就要扫 `Content-Length:`，量级相同）。
> 想更省，可以把"头尾偏移"存进状态机，一次请求只算一次。

### 1.4 验证

修复后重跑：分片 POST 三种姿势全部 **200 + body 完整 + 进程存活**；功能回归 **PASS 27 / FAIL 0**；valgrind **definitely lost 0**。

---

## 2. N2（P1）：解析失败被伪装成 `200 OK` 空响应

### 2.1 现象

| 请求 | 修复前 | 应然 | 修复后 |
|---|---|---|---|
| URL 长度 8192 / 8300 / 9000 / 20000 | **200 OK，`Content-Length: 0`，无 body** ❌ | 414 | **414 URI Too Long ✅** |
| 单个头 20KB（> `MAX_HEADER_LEN` 16KB） | **200 OK，空 body** ❌ | 431 | **431 Header Fields Too Large ✅** |
| URL 8000、头 10KB、未知路由、未知方法 | 404 / 404 / 400 ✅ | —— | 不变 ✅ |

修复前那三个数字的完整响应是：

```
HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n
```

→ **客户端/爬虫会以为请求成功**，而实际上这个请求根本没被处理（静默吞掉）。这种"错误码丢失"比 503 更危险：调用方无法感知失败。

### 2.2 根因（两层）

**① 底层检查其实是对的**：`RequestAnalysiser.c:93/103` 发现 URL 过长会 `*failReason=414; return 0;`，`DealHeaders` 里也有 `431`。

**② 但状态码没被带回响应对象**（`Threads/WorkerThread.c`）：

```c
int Error_reason=t.error_reason;                       // 200
Response Rsp={ .error_reason=Error_reason, ... };      // ← Rsp 在这里就被固定成 200 了
if(Error_reason==200){
    RequestGet(profes,Http,&h,Size,&Error_reason,W);   // 这里把 Error_reason 改成 414/431
    if(Error_reason==200){ DealWithTask(...); }        // 解析失败 → 跳过业务
}                                                      // ← 但没人把 414/431 写回 Rsp.error_reason
PackTotal(...); PackBackHead(...);                     // Rsp.error_reason 仍是 200 ⇒ 发 200 空响应
```

另外 `RequestAnalysisHead()` 调用 `DealUrlAndQuery()` / `DealHeaders()` 时**没有检查它们的返回值**（`RequestAnalysiser.c:176,179`），不过因为错误码已经写进 `*error_reason`，所以 ② 修好后行为就对了；① 建议顺手也检查（防御性）。

### 2.3 修法（已落地，2 行）

```c
        if(Error_reason==200){
            ...DealWithTask(&Req,&Rsp); RequestAnalysisFree(...);
        }else{
            Rsp.error_reason=Error_reason;   /* 【修复】解析失败的状态码同步回响应对象 */
        }
```

---

## 3. N3（P1，待你决策）：请求体一超过 32KB 就必然 503

### 3.1 现象（一次发完的 POST，`/api/echo` 回显）

| 请求体大小 | 1KB | 16KB | **32KB** | 64KB | 256KB | 1MB |
|---|---|---|---|---|---|---|
| 结果 | 200 ✅ | 200 ✅ | **503 + RST** | 503 | 503 | 503 |

而文档/注册表里的上限写的是 `MAX_BODY_SIZE (1024*1024)` = 1MB —— **实际能用的上限只有 ~32KB**。

### 3.2 根因（内存池插桩给的现场）

```
ALLOCFAIL pool=0x...7890 size=64KB class=6 strips=0 inuse=0 freeTop=0
```

- `strips=0` 的关键含义：这个尺寸类**根本没预分配**（不是被用光）。
- 那个池是 `ReceiverThreadInit()` 里的暂存区：
  `MemoryPoolInit(&store_area, 10, 6, 500, 400)` → 类 1KB~512KB，但 **`init_max=6` 只给前 6 类（1KB~32KB）各 400 条；64KB~512KB 四类都是 0 条**。
- 而 `MemoryPoolAlloc` 是**非扩容**语义（`MemoryEntryAllocTwo`，满了/不存在就 `notfull=0`）。
- 请求（头+体）超过 32KB 时，暂存区需要 `RequestTempPoolExpend()` 扩到 33KB → 落在 64KB 类 → `notfull=0` → `hadBigest=1` → `FdClose(503)`。

### 3.3 三个方案（各有取舍，你选）

| 方案 | 改法 | 优点 | 代价 |
|---|---|---|---|
| **A. 扩大预分配** | `ReceiverThreadInit` 里 `MemoryPoolInit(&store_area, 10, **10**, 500, 400)`（或把大类的条数调小） | 一行改动，立刻支持大请求 | 预分配 400×(64+128+256+512)KB ≈ **384MB 虚拟地址空间**（calloc 是惰性映射，RSS 只在实际用到时增长，但 VSZ 会变很大；也可以把大类的 init 条数改小，需要小改 `MemoryPoolInit` 支持"每类不同条数"） |
| **B. 暂存区走 malloc 兜底** | 在 `RequestTempPoolAlloc/Expend` 里：当需求超过池最大类时，直接 `malloc` 并把"是否池内存"记下来，释放时对应 `free`（`MemoryPoolFree` 已经有"超出最大类→free"的判定，但 `RequestTempPoolFree` 需要传对尺寸） | 不浪费内存，真正支持到 1MB | 要小心改内存归属逻辑（约 10 行） |
| **C. 明确上限** | 把 `MAX_BODY_SIZE` 降到与实现一致（如 32KB），超限直接回 **413**；文档同步 | 0 风险、语义清晰 | 大 POST 用不了（上传/大表单场景受限） |

> 我的建议：**先做 C（立刻消除"文档说 1MB 但实际 503"的误导），再做 B（想要 1MB 支持）**；
> A 只适合"并发不高、内存宽裕"的场景。

---

## 4. N4（P2，建议）：协议语义与安全

| 用例 | 实测行为 | 建议 |
|---|---|---|
| `Connection: close` | **被忽略**，连接继续复用（200 + 保持） | 尊重它：读完当前响应后关闭（否则客户端/代理的连接池行为异常） |
| `HTTP/1.0`（无 keep-alive 头） | 200 + 保持连接 | HTTP/1.0 默认应关闭连接 |
| `HEAD /hello` | **405 Method Not Allowed** | 至少支持 HEAD（同 GET 但不发 body），很多监控/CDN 依赖它 |
| 重复 `Content-Length`（3 和 5） | **200，不报错**（用了其中一个） | RFC 7230 要求这种情况直接 **400**；否则在与反向代理同用时存在**请求走私**风险 |
| `Transfer-Encoding: chunked` | 不认识 TE，按"无 body"处理 → 后续字节被当成新请求 | 要么支持 chunked，要么**同时出现 TE 与 CL 时回 400**（同样是走私风险点） |
| 请求体 > 上限 | 见 N3 | 见 N3 |
| 无 `Host` 头（HTTP/1.1） | 200（宽松接受） | 公网部署建议回 400 |
| `GET /../Makefile` | 404 ✅（路由精确匹配，不做文件映射） | 保持 |

---

## 5. 附：本轮 22 个边界用例的完整结果（修复前）

| 用例 | 状态行 | 结束方式 | 服务器存活 |
|---|---|---|---|
| C1 未知方法 FOO | 400 Bad Request | 服务端关闭 | ✅ |
| C2 纯垃圾字节 | 400 Bad Request | 服务端关闭 | ✅ |
| C3 `Content-Length: 2000000` | 413 Payload Too Large | 服务端关闭 | ✅ |
| C4 超长 URL 9KB | **200 OK（空 body）** ❌ 见 N2 | 读超时 | ✅ |
| C5 超长头 20KB | **200 OK（空 body）** ❌ 见 N2 | 读超时 | ✅ |
| C6 `Content-Length:100` 只发 50 | （无响应，正确行为：等补齐） | 读超时 | ✅ |
| **C7 POST 体分两段** | **无响应** | 服务端关闭 | ❌ **崩了（N1）** |
| C8 POST 1MB 一次发 | 503（N3） | RST | ✅ |
| **C9 POST 1MB 分 10 段** | **无响应** | RST | ❌ **崩了（N1）** |
| D1 管道化 GET+GET | 200 + 顺序正确 | 读超时 | ✅ |
| D2 HEAD /hello | 405（N4） | 服务端关闭 | ✅ |
| D3 `Connection: close` | 200（未遵守 close，N4） | 读超时 | ✅ |
| D4 无 Host 头 | 200 | 读超时 | ✅ |
| D5 HTTP/1.0 | 200 | 读超时 | ✅ |
| D6 双 Content-Length | 200（N4 走私风险） | 读超时 | ✅ |
| D7 `Transfer-Encoding: chunked` | 200（N4 走私风险） | 服务端关闭 | ✅ |
| **D8 同连接先 GET 再分片 POST** | **200 后崩溃** | 服务端关闭 | ❌ **崩了（N1）** |
| D9 `Content-Length: 0020` | 200（前导零正常） | 读超时 | ✅ |
| D10 `GET /../Makefile` | 404（安全） | 服务端关闭 | ✅ |
| D11 8KB 查询串 | 200 | 读超时 | ✅ |

补充：每个用例前后 `/proc/<pid>/fd` 计数都是 5→5（**无 fd 泄漏**）。

---

## 6. 修复清单（累计到本轮）

| 文件 | 改动 | 解决 |
|---|---|---|
| `Threads/WorkerThread.c` | ① 错误响应不再分配打包块 + 补 `PackBackHead` 回收<br>② `RspHeaderAlloc` 池满置 NULL 并 return<br>③ 解析失败状态码同步回 `Rsp.error_reason` | 上一轮 P0-1（全站 503 / 段错误）+ **本轮 N2**（200 空响应） |
| `FdConnectResource/RequestTempPool/RequestTempPool.c` | `RequestTempPoolDestory` 里 `free(h->state_machine)` | 每连接 12 字节 malloc 泄漏（RSS 持续上涨） |
| `RequestCome/RequestIntergrity/RequestIntergrity.c` | `target` 初始化 + `h_rnrn==1` 时重新定位头尾 | **本轮 N1（崩溃）** |
| `AgentSpace/P0_TWO_BUGS_REPORT.md` | 上一轮报告（含第 7 节修复记录） | —— |
| `AgentSpace/CODE_AUDIT_FINDINGS.md` | 本报告 | —— |
| `.vscode/tasks.json`、`Makefile` | 之前为 `foo.h` 加的 `-I` / `-IAgentSpace`、`SRCS` 过滤 | 构建配置 |

### 验收快照（四处修复一起）

| 项目 | 结果 |
|---|---|
| 网站功能测试 27 项 | **PASS 27 / FAIL 0** ✅ |
| valgrind（150 个 404 + 15 个 hello） | **definitely lost 0 bytes / 0 blocks** ✅ |
| 60 秒 404 压测 | RSS 稳定 3384 kB、无 503、rps 稳定 17~18k ✅ |
| 分片 POST / 分两段 POST / 同连接混合 | 200 + body 完整 + 进程存活 ✅ |
| URL>8192 / 头>16KB | 414 / 431 ✅ |

---

## 7. 还剩什么（按优先级）

| 优先级 | 问题 | 说明 |
|---|---|---|
| 1 | **N3 大请求 503** | 需要你在第 3 节的 A/B/C 里选一个 |
| 2 | **P0-2 流水线丢请求** | 多连接一次性发很多请求时偶发丢响应 / 连接假死（见 `P0_TWO_BUGS_REPORT.md` 第 3 节），修法：接收线程"续跑"机制 + tempPack 环形槽 + 序号后移 |
| 3 | **每请求 ~1.1ms 延迟地板** | 响应发送只靠接收线程 `epoll_wait(...,1)` 的 1ms tick；建议用 eventfd/条件变量唤醒 |
| 4 | N4 协议语义 | 公网部署前建议至少处理"重复 CL / TE 与 CL 并存"（走私风险）和 `Connection: close` |


