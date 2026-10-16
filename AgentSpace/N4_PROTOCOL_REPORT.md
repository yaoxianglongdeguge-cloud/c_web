# N4 报告：HTTP 协议语义 与「请求走私」风险说明

> 本报告由 Cline 生成，**没有改动任何源码**，只解释"现在是什么行为、会出什么事、建议怎么改"。
> 所有"实测"都是对当前仓库代码跑出来的（原始 socket 发字节，见第 4 节可自行复现）。

---

## 0. 一句话总结

这些不是"崩溃/泄漏"级别的问题，而是**协议语义不完整**：大多数情况下只是"不够标准、兼容性差"，**但其中两条（重复 `Content-Length`、`Transfer-Encoding: chunked`）在你将来把服务器放到 nginx/CDN/云负载均衡后面时会变成安全漏洞（请求走私）**。
**现在：自己直连、内网自用 → 风险低；一旦前面挂了代理 → 必须先修这两条。**

| # | 项目 | 现状（实测） | 直接后果 | 风险等级 | 建议 |
|---|---|---|---|---|---|
| 1 | `Connection: close` | **被忽略**，连接继续复用 | 客户端/代理的连接池行为异常；某些客户端会一直等连接关闭 | 🟡 中 | 遵守：读完当前响应后关闭 |
| 2 | HTTP/1.0 请求 | 200 且保持连接 | HTTP/1.0 默认应"响应完就关" | 🟡 中 | HTTP/1.0 默认关连接 |
| 3 | `HEAD` | **405 Method Not Allowed** | 监控/CDN/健康检查、`curl -I` 都会失败 | 🟡 中 | 支持 HEAD（等同 GET，但不发 body） |
| 4 | **重复 `Content-Length`** | **200，不报错**（用了其中一个） | 与代理配合时**请求走私**（见第 3 节） | 🔴 高（带代理时） | 直接回 **400** |
| 5 | **`Transfer-Encoding: chunked`** | 不认识 TE，按"无 body"处理；残留字节被当成下一个请求 | **请求走私**、请求错乱 | 🔴 高（带代理时） | 同时出现 TE 与 CL → 400；或支持 chunked |
| 6 | 无 `Host` 头（HTTP/1.1） | 200（宽松接受） | 虚拟主机场景会路由错 | 🟢 低 | 公网部署建议 400 |

---

## 1. 逐条讲清楚

### 1.1 `Connection: close` 被忽略（表 #1）

**实测**：发一个带 `Connection: close` 的请求，服务器回了 200 之后**并没有关闭连接**：

```
请求：GET /hello HTTP/1.1\r\nHost: c\r\nConnection: close\r\n\r\n
响应：HTTP/1.1 200 OK ... （连接保持，客户端读超时才收场）
```

**为什么会这样**：代码里**根本没解析 `Connection` 头**（请求头都在哈希表里，但没人读它），响应头里也从来不写 `Connection: close`。
唯一会关连接的情况是：请求出错（400/404/405…）时由 `FdClose()` 写错误响应再关。

**会出什么事**：
- 客户端（curl/浏览器/各种 SDK）以为你说好要关，结果连接一直开着 → 可能复用、可能等超时，行为不一致；
- 反向代理默认按 HTTP/1.1 规则**复用连接**，它以为你会关，结果把它自己的连接池搞乱。

**怎么改**（改法很小）：解析 `Connection` 头；若值含 `close`（或 HTTP/1.0 无 `keep-alive`），就在响应头里加 `Connection: close`，并在这次响应发完后调用现有的 `FdClose(...,200)`（注意用 200 表示"正常关闭"，不要发错误文本）。

### 1.2 HTTP/1.0 请求也保持连接（表 #2）

**实测**：`GET /hello HTTP/1.0` → `200 OK` 且连接保持。
HTTP/1.0 的默认语义是**短连接**（除非显式 `Connection: keep-alive`）。现在等于对 1.0 客户端也强行长连接。
**改法**：与 #1 一起做——HTTP/1.0 且没有 `Connection: keep-alive` → 响应后关闭。

### 1.3 `HEAD` 返回 405（表 #3）

**实测**：`HEAD /hello` → `405 Method Not Allowed`。
**为什么会这样**：框架的方法表里**没有 HEAD**（`RequestIntergrity.c` 的 `Method_p[]` 只有 GET/DELETE/HEAD?… 实际上表里有 "HEAD "，但业务层 `WebApp.c` 的静态路由写了"只允许 GET，否则 405"，而 HEAD 落到 405 分支）；另外响应构造也只会带 body。
**会出什么事**：很多工具靠 HEAD 探活/取 Content-Length：`curl -I`、监控系统、CDN 回源校验、S3 风格客户端。全都会拿到 405。
**改法**：两处配合——① 业务层把 HEAD 当作 GET 处理（只是不发 body）；② 框架层在 HEAD 时**不发 body 但仍发 `Content-Length`**（需要给 `Response` 加一个"head_only"标记，或者由业务层决定）。

### 1.4 重复 `Content-Length`（表 #4）——**重点**

**实测**：`Content-Length: 3` 和 `Content-Length: 5` 同时出现 → **服务器没有报错**，按其中一个处理并返回 200。
**为什么会这样**：解析器把请求头塞进哈希表时**后一个会覆盖前一个**（或前者保留），没人检查"这个头出现了几次"，也没有"两个值不一致就报错"的逻辑。
**RFC 7230 §3.3.2/§3.3.3 的要求**：**任何一个消息里出现多个 `Content-Length`（值不同）就必须当作错误**（回 400 并关闭连接）。
**为什么要较真**：见第 3 节的走私原理——**前后端对"请求体有多长"的理解必须完全一致**，只要有两个 CL，攻击者就可能让前端按 3 字节、后端按 5 字节，从而"藏"起一段字节当**下一个请求**。

### 1.5 `Transfer-Encoding: chunked` 不认识（表 #5）——**重点**

**实测**：发 `Transfer-Encoding: chunked` + `5\r\nhello\r\n0\r\n\r\n` → 服务器返回 200（因为按"没有 body"处理），**残留的那种分块字节被当成下一个请求去解析** → 结果是 `400 Bad Request` 或连接被关。
**为什么会这样**：代码里只有 `Content-Length` 这一种长度来源（`RequestIntergrity.c` 里搜索 `"Content-Length:"`）；**完全不认识 `Transfer-Encoding`**。
**会出什么事（两类）**：
1. **兼容性**：现代客户端（`curl -T`、Node/Python 的 http 客户端在上传流式 body 时）默认用 chunked → 请求会被解析错。
2. **安全（走私）**：如果前面有代理，代理可能会把 chunked 请求**规范化**（重新计算长度）再转发给你——双方对边界的理解不同，就会出现"走私"。
**改法（二选一）**：
- **最省事、最安全**：请求里**同时**出现 `Transfer-Encoding` 和 `Content-Length`，或者出现 `Transfer-Encoding: chunked` 而你不打算支持 → **直接 400 并关闭连接**（RFC 也允许这样）。
- 想完整支持：在状态机里加 chunked 解码（解析 chunk 大小 + `\r\n` + 数据 + 结束块），复杂度明显上升，建议以后再做。

### 1.6 无 `Host` 头（表 #6）

HTTP/1.1 要求必须有 `Host`。现在是"缺了也照常 200"。自用没问题；如果将来做虚拟主机/多域名，缺 Host 会导致路由到错误站点。**建议**：公网部署时缺 Host → 400。

---

## 2. 已顺带修好的两条（对照参考）

| 项目 | 修复前 | 修复后 |
|---|---|---|
| URL > 8192 | `200 OK` 空响应（静默失败）❌ | **414 URI Too Long** ✅ |
| 单个头 > 16KB | `200 OK` 空响应 ❌ | **431 Header Fields Too Large** ✅ |

（见 `CODE_AUDIT_FINDINGS.md` 的 N2：根因是 `Rsp.error_reason` 没同步，已修。）

---

## 3. 什么是"请求走私"（用大白话说）

想象一条**流水线**：客户端 → **代理（nginx/CDN）** → 你的服务器。
代理和你的服务器**各自**要判断"这个请求到哪儿结束、下一个请求从哪儿开始"。判断依据就是 `Content-Length` 或 `Transfer-Encoding`。

**如果两者判断不一致**，就有一截字节被"藏"起来：

```
攻击者只发一个 TCP 流：
   POST /x HTTP/1.1
   Content-Length: 3          ← 代理按这个看：body = "abc"
   Content-Length: 5          ← 你的服务器按这个看：body = "abcXY"

解析结果：
   代理：  [POST /x  body=abc]               剩下 "XY" 它认为不完整，先攒着
   你的服务器：[POST /x  body=abcXY]         ← 边界不一致！

于是攻击者可以在 "XY" 里塞一个【完整的第二个请求】，
   代理以为是某个受害用户的连接上的普通字节，
   你的服务器却把它当成一个新请求执行了。
→ 结果：绕过鉴权、命中别人不该命中的接口、缓存投毒等。
```

**你现在安全吗？**
- **直连、内网自用**：没有"第二个人"来解析同一个流 → 基本用不上这个漏洞，所以**可以不急着改**。
- **将来挂 nginx / CDN / 云 LB / 任何代理**：**必须先处理**（#4 和 #5），因为那时就有了"两个解析者"。
- 参考：CVE 里大量 Nginx/Gunicorn/Tomcat 的走私漏洞，都是这一类"边界不一致"。

---

## 4. 你可以自己复现（复制就能跑）

```bash
# ① Connection: close 被忽略（响应后连接不关，会一直等）
printf 'GET /hello HTTP/1.1\r\nHost: c\r\nConnection: close\r\n\r\n' | timeout 3 nc 127.0.0.1 5343

# ② HEAD 被拒
curl -sI http://127.0.0.1:5343/hello          # 观察：405 Method Not Allowed

# ③ 重复 Content-Length：服务器不报错（应 400）
printf 'POST /api/echo HTTP/1.1\r\nHost: c\r\nContent-Length: 3\r\nContent-Length: 5\r\n\r\nabc' | timeout 3 nc 127.0.0.1 5343

# ④ chunked：残留字节被当成下一个请求
printf 'POST /api/echo HTTP/1.1\r\nHost: c\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n' | timeout 3 nc 127.0.0.1 5343

# ⑤ HTTP/1.0：不会自动关连接
printf 'GET /hello HTTP/1.0\r\n\r\n' | timeout 3 nc 127.0.0.1 5343
```

---

## 5. 建议的改动顺序（本次**没有**改任何代码，等你决定）

| 顺序 | 改什么 | 工作量 | 触发条件 |
|---|---|---|---|
| 1 | #4 重复 `Content-Length` → **400** | 很小（解析时记"CL 出现过几次/值是否一致"） | **只要打算挂代理就必须做** |
| 2 | #5 `TE` 与 `CL` 同时出现 → **400**（或支持 chunked） | 小～中 | 同上 |
| 3 | #1/#2 `Connection: close` / HTTP/1.0 → 响应后关连接 | 小 | 兼容性、代理行为正确 |
| 4 | #3 支持 `HEAD` | 中（要给响应加"只发头"的标记） | 监控/CDN/`curl -I` |
| 5 | #6 缺 `Host` → 400 | 很小 | 公网/虚拟主机 |

> 我的建议：**直连自用阶段可以都不改**；等到要"对外 + 挂代理"，先做 1、2，再做 3、4。
