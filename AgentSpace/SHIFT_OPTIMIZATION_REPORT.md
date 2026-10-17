# 移位优化候选清单（只找不改）

> 本文件由 Cline 生成，**没有修改任何源码**（只做了 grep + 反汇编 + 微基准）。
> 结论写在最前面，后面是逐条清单和复现方法。

---

## 0. 结论（有点反直觉，先看这个）

| 类别 | 能不能靠移位提速 | 原因（实测证据） |
|---|---|---|
| **常量是 2 的幂**的 `*1024` `/1024` `%1024` `*2` `/2` … | ❌ **不能，也不用改** | `-O0` 下 GCC **已经**变成 `salq/shrq/andl`；实测两种写法都是 **0.45 ns/op**（见 §3 反汇编） |
| **常量不是 2 的幂**的 `*33` `*56` `*100` | ❌ **不要改**（改成移位加法反而更慢） | `*33` 已自动变 `salq $5 + addq`（0.649 vs 0.650 ns）；`*56`/`*100` 用 `imulq` 是 **0.83/0.85 ns**，手写移位加法是 **0.90/0.91 ns** ⇒ 乘法更快 |
| **除数是变量**的 `%` 和 `/`（`divq`/`idivq`） | ✅ **唯一真正值得改的地方** | `%100` = **2.26 ns**、`%7` = **2.52 ns**，而移位/与运算版本是 **0.45 ns** ⇒ 慢 **5 倍**、每处省 **~1.8~2.1 ns** |

**量级提醒**：全代码里这类"变量取模"每请求大约出现 **8~47 次**（路由 1 次 + 每个请求头 1 次 + fd 表若干 + 发送槽 3 次），
全部改完大约省 **15~90 ns/请求**，相对当前 **~13 µs/请求** 只是 **0.1%~0.7%**。
→ 属于"**顺手改**"级别，不是性能大头（真正的大头是刚修掉的 1.1 ms 延迟地板）。

---

## 1. 清单 A：真正值得改的（变量除数 → 移位/与）

### A-1 哈希表桶定位（3 处，热路径）

| 文件:行 | 现在的写法 | 改成 | 每请求调用次数 |
|---|---|---|---|
| `Router/RouterDataStruct/RouterHash.c:241` | `return hash(url, salt) % bucket_size;` | `return hash(url, salt) & (bucket_size-1);`（**桶数改成 2 的幂**，如 7→8、17→16…） | 1 次（路由查找） |
| `RequestCome/RequestAnalysis/AnalysiserDataStruct/AnalysiserHash.c:160` | `return hash_2(url) % bucket_size;` | 同上 | **每个请求头 + 每个查询参数 1 次**（40 个头的请求 = 41 次） |
| `FdConnectManage/FdConnectTable/FdConnectTable.c:192` | `{ return url % bucket_size; }` | 同上 | 每请求 2~3 次（查连接对象）+ 每次 accept/close |

**怎么改**：把 `FdConnectTable.c:4` 的 `PRIME_BUCKET_SIZES_3[]`（7/17/31/59/113…）、
`RouterHash.c` 与 `AnalysiserHash.c` 的桶尺寸表都换成 **2 的幂序列**（8/16/32/64…），
然后 `% bu_num` → `& (bu_num-1)`。
**注意**：① 扩容阈值也要一起看（`RouterHash.c:217` 是 `elem_num > bu_num*2`）；
② 桶数从质数改成 2 的幂会改变冲突分布——DJB2 的低位是混合过的，通常没问题，但**建议改完用真实路由/头集合量一下平均探查长度**（我可以帮你写这个对比脚本）。

### A-2 有序发送槽的环形下标（3 处，每个响应都走）

| 文件:行 | 现在的写法 | 改成 |
|---|---|---|
| `ResponseSend/ResponseSendMain.c:5` | `int next=s->serial%fd_ob->send_pointer_pool->blocknum;` | `serial & (blocknum-1)` |
| `ResponseSend/ResponseSendMain.c:93` | `int next=fd_ob->ser_nex_send%fd_ob->send_pointer_pool->blocknum;` | 同上 |
| `ResponseSend/ResponseSendMain.c:169` | `next=(next+1)%fd_ob->send_pointer_pool->blocknum;` | `next=(next+1) & (blocknum-1);` |

**前提**：`blocknum` 现在是 `ed_store_blocknum = 100`（**不是 2 的幂**）→ 要把它改成 128 或 256。
改它的副作用：每个连接的在途请求上限从 100 变成 128（对内存占用/背压有一点影响，属于配置取舍）。
> 如果不想动这个配置：也可以保留 `%100`，代价是每个响应多花 ~2ns（3 处 ≈ 6ns/请求）。

### A-3 内存池里的"除以变量"（1 处；你换新内存池时**特别值得顺手做**）

| 文件:行 | 现在的写法 | 改成 |
|---|---|---|
| `MemoryPool/MemoryPool.c:140` | `int serial=total_gap/(size*1024);`（`size` 是"这类内存块的 KB 数"） | `int serial=total_gap>>(__builtin_ctz(e->strip_size)+10);` |

**为什么可行**：`strip_size` 在 `MemoryPoolInit` 里是从 1 开始 **每次 ×2** 生成的 ⇒ 一定是 2 的幂，
所以"除以 strip_size"就等于"右移 log2(strip_size)"；而 `__builtin_ctz` 在 x86 上就是一条 `tzcnt/bsf`（~1ns），比 `divq`（~2.3ns）便宜。
> 更彻底的做法：在建池时就把 `strip_shift = log2(strip_size)` 存进 `Memory_Entry`，用的时候直接 `>> (strip_shift+10)`（零计算）。
> 另外 `MemoryPool.c:186`（选尺寸类）和 `:206`（归还时选类）是两个 `alloc_size=alloc_size*2` 的循环，
> 可以用 `1<<(32-__builtin_clz(size-1))` 这类位扫描一次算出来，省掉循环分支（对每请求好几路的池分配/归还有意义）。

---

## 2. 清单 B：常量是 2 的幂的运算（**编译器已优化，改了没有收益**，仅列出备查）

> 这一节是为了回答"还有哪些地方看起来能移位"——它们的**移位写法更直白但不会更快**，
> `-O0` 下 GCC 已经生成 `sal/shr/and`。要不要改纯看你对可读性的偏好。

| 文件:行 | 原式 | 等价的移位写法 |
|---|---|---|
| `FdConnectManage/Timer/TimerDataStruct/PriorQueueTimer.c:50/52/55` | `t/2` | `t>>1` |
| 同上 `:67/69/71/72/75/79/80/81/84/88/89/92` | `t*2`、`t*2+1` | `t<<1`、`(t<<1)+1` |
| `FdConnectResource/RequestTempPool/RequestTempPool.c:54` | `size*1024` | `size<<10` |
| 同上 `:78` | `nowsize*2/1024+1` | `(nowsize>>9)+1` |
| 同上 `:90` | `expendsize*1024-1` | `(expendsize<<10)-1` |
| `FdConnectResource/SendPointerPool/SendPointerPool.c:16` | `sizeof(...)*blocknum/1024+1` | `(sizeof(...)*blocknum)>>10)+1`（注意 `*sizeof` 不是 2 的幂，见 §0） |
| 同上 `:71` | `blocknum*sizeof(...)` | 同上（`imul` 更快，建议不动） |
| `MemoryPool/MemoryPool.c:77/91/93` | `strip_size*1024`、`before_num*strip_size*1024`、`expend_num*strip_size*1024` | `<<10` 形式 |
| 同上 `:88/98/165` | `before_num*2`、`strip_num*2`、`strip_size*2` | `<<1` |
| 同上 `:112/126` | `e->strip_size*1024` | `<<10` |
| 同上 `:179` | `malloc(size*1024)` | `malloc(size<<10)` |
| 同上 `:199` | `strip_size*1024` | `<<10` |
| 同上 `:205/206` | `alloc_size*1024>=size`、`alloc_size=alloc_size*2` | `(alloc_size<<10)>=size`、`alloc_size<<=1` |
| 同上 `:210` | `strip_size*strip_num*1024` | `(strip_size*strip_num)<<10` |
| `RequestCome/RequestIntergrity/RequestIntergrity.c:4` | `#define MAX_BODY_SIZE (1024*1024)` | 常量折叠，无所谓 |
| `Router/RouterDataStruct/RouterHash.c:217` | `h->bu_num*2` | `bu_num<<1` |
| `Threads/WorkerThread.c:52` | `*allocsize/1024+1` | `(*allocsize>>10)+1` |
| 同上 `:69` | `(m.size/1024+1)*1024` | `((m.size>>10)+1)<<10` |
| 同上 `:223/246` | `(w->body_size/1024+1)*1024` | `((w->body_size>>10)+1)<<10` |
| 同上 `:262` | `1024*(size/1024+1)` | `((size>>10)+1)<<10` |
| `RequestAndResponse/Response.c:8` | `len/1024+1` | `(len>>10)+1` |
| `RequestCome/RequestAnalysis/RequestAnalysiser.c:19/23` | `(h_size/1024)+1`、`1024*((h_size/1024)+1)` | `(h_size>>10)+1`、`((h_size>>10)+1)<<10` |
| `RequestCome/RequestComeMain/RequestComeMain.c:63` | `h_size/1024+1` | `(h_size>>10)+1` |
| `AgentSpace/WebApp/WebApp.c:92` | `hi*16+lo` | `(hi<<4)+lo` |
| `AgentSpace/WebApp/WebApp.c:28` | `#define MEM_BODY_SIZE (256*1024)` | 常量，无所谓 |
| `Router/RouterDataStruct/RouterHash.c:230`、`AnalysiserHash.c:154` | DJB2 `h = h*33 + c` | `h = ((h<<5)+h) + c`（**实测无收益**：`-O0` 已是 `salq $5`+`addq`） |

---

## 3. 证据：`-O0` 下 GCC 到底生成什么（反汇编）

```bash
$ printf 'unsigned long f(unsigned long x){return x*1024;}\n
         unsigned long g(unsigned long x){return x/1024;}\n
         unsigned long h(unsigned long x){return x%1024;}\n
         unsigned long i(unsigned long x,unsigned long y){return x%y;}\n' > o0check.c
$ gcc -O0 -S -o - o0check.c | grep -E 'imul|sal|shr|div|and'
        salq    $10, %rax        ← x*1024  已被变成左移
        shrq    $10, %rax        ← x/1024  已被变成右移
        andl    $1023, %eax      ← x%1024  已被变成按位与
        divq    -16(%rbp)        ← x%y（除数是变量）只能真除法，这就是慢的那类
```

DJB2 与非 2 的幂常量：

```bash
$ printf 'unsigned long f(unsigned long x){return x*33;}\nunsigned long g(unsigned long x){return x*56;}\n' > o0c2.c
$ gcc -O0 -S -o - o0c2.c | grep -E 'imul|sal|add'
        salq    $5, %rax         ← x*33 = (x<<5)+x，已经被分解成移位+加法
        addq    %rdx, %rax
        imulq   $56, %rax, %rax  ← 56 不是 2 的幂，用乘法指令（而且它更快，见下）
```

## 4. 证据：微基准实测（20,000,000 次/轮，7 轮取最小值，单位 ns/op）

`-O0`（你现在的编译参数）：

| 写法 A | A 耗时 | 写法 B（移位版） | B 耗时 | 结论 |
|---|---|---|---|---|
| `x = x*1024 + 1` | **0.458** | `x = (x<<10) + 1` | **0.454** | 一样（已优化） |
| `x = x*2 + 1` | **0.485** | `x = (x<<1) + 1` | **0.490** | 一样 |
| `x = x/1024 + 1M` | **0.451** | `x = (x>>10) + 1M` | **0.460** | 一样 |
| `x = x%1024 + 1M` | **0.447** | `x = (x&1023) + 1M` | **0.447** | 一样 |
| `h = h*33 + 7` | **0.649** | `h = ((h<<5)+h) + 7` | **0.650** | 一样 |
| `y = y*56 + 1` | **0.830** | `y = (y<<5)+(y<<4)+(y<<3) + 1` | **0.899** | **乘法更快** |
| `y = y*100 + 1` | **0.849** | `y = (y<<6)+(y<<5)+(y<<2) + 1` | **0.905** | **乘法更快** |
| **`x = x%100 + 1M`（变量模数）** | **2.264** | 若能改成 `& 127`（2 的幂）则为 0.45 | —— | **慢 5 倍 ⇒ 值得改** |
| **`x = x%7 + 1M`（变量模数）** | **2.524** | 若能改成 `& 7` 则为 0.45 | —— | **慢 5.6 倍 ⇒ 值得改** |

（`-O2` 下同一组数字几乎一样，说明**这就是两条指令的真实差距**，不是编译参数问题。）

## 5. 复现方法（自行验证）

```bash
# 反汇编
printf 'unsigned long f(unsigned long x){return x*1024;}\nunsigned long g(unsigned long x){return x/1024;}\nunsigned long h(unsigned long x){return x%1024;}\nunsigned long i(unsigned long x,unsigned long y){return x%y;}\n' > /tmp/o0check.c
gcc -O0 -S -o - /tmp/o0check.c | grep -E 'imul|sal|shr|div|and'

# 微基准（源码见本报告 §4 对应的两个小程序，或让我贴出来）
gcc -O0 -o /tmp/b1 /tmp/shift_bench.c  && /tmp/b1     # 常量 2 的幂 / 变量取模
gcc -O0 -o /tmp/b2 /tmp/shift_bench2.c && /tmp/b2     # *33 / *56 / *100
```

## 6. 建议的动手顺序（如果真要改，和"值不值"的判断）

| 顺序 | 改哪里 | 预计收益 | 风险/代价 |
|---|---|---|---|
| 1 | **`MemoryPool.c:140`** 那处 `/(size*1024)` → 移位（或用 `strip_shift`） | 每次池归还省 ~1.8ns；你换新内存池时最值得做 | 低（`strip_size` 必是 2 的幂，前提成立）；顺带把 `:184/:203` 两个 ×2 循环换成位扫描 |
| 2 | `ResponseSendMain.c` 三处 `%blocknum` | 每响应省 ~4~6ns | 中（要把 100 改成 128/256，影响在途上限） |
| 3 | 三处哈希桶 `% bucket_size` → `& (n-1)` | 每请求省 15~90ns（头多时更明显） | 中（桶尺寸表要换成 2 的幂，**冲突率需要复测**） |
| — | 清单 B（常量乘除模） | **0** | 不建议为性能去改；想改只为可读性 |

> 一句话：**"能用移位优化"的地方我都列出来了，但只有"除数是变量"的那几处真能变快（每处 ~2ns、每请求合计 15~90ns）；常量那些编译器早就替你做完了。**

---

## 7. 本次改动声明

- **没有修改任何 `.c/.h` 文件**，也没有改 Makefile/脚本；
- 只做了：全代码 grep、`gcc -O0 -S` 反汇编、两个独立的微基准程序（放在 `/tmp`）、以及本报告文件的创建；
- 报告里所有 `文件:行` 都是按当前仓库工作区的行号写的（`grep -n` 可直接定位）。

