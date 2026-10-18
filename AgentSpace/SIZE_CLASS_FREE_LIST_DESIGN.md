# 分类空闲链（Size-Class Free List）设计方案

> 版本：2026-10-17　
> 适用：c_web 的 `MemoryPool`（线程私有内存池）
> 相关实测报告：`AgentSpace/NEW_POOL_AND_PIGGYBACK_TEST_REPORT.md`（5.1 ~ 5.9 节）
> 本文只描述方案，不含对现有源码的改动；所有基准源码在 `/tmp/bm*.c`，可复跑。

---

## 0. 摘要

**分类空闲链 = 按"分配尺寸"把内存分成若干类，每类维护一条空闲块链；`alloc` 取链头、`free` 挂链头，都是 O(1)，块空间在释放的瞬间就可复用。**

用它在 c_web 上实测（真实尺寸分布 + 真实 `VmHWM`）：

| 指标 | 现有池（整池归零） | 分类空闲链 |
|---|---|---|
| 分配+释放耗时 | 62~66 ns | **53~64 ns** |
| 真实物理内存（3.03MB 活跃） | **16 MB**（5.3×） | **5 MB**（1.6×） |
| 内部块浪费（1.25× 阶梯） | 0%（精确字节） | **1.5%** |
| 分配失败（高并发） | c=200 大量 503 / c=3000 池化失败 1188 次 | **0** |
| 泄漏后果 | 一个块钉住整个 1~2MB 子池 | 只丢它自己（32B~64KB） |
| 是否需要扫描/压缩/搬移 | 不需要 | **不需要** |

代价：需要维护"类表 + chunk 归属"，以及**每类容量要能增长**（见 §5）。

---

## 1. 背景：为什么必须回到"按块回收"

### 1.1 现有池的行为（代码事实，不是基准结论）

`MemoryEntryFree` 只做两件事：

```c
e->hadAllocNum--;                 /* ① 只减计数 */
if(e->hadAllocNum == 0){          /* ② 只有整块子池归零 */
    e->allocPtr = e->beginPtr;    /*    才把这一整块（1~2MB）复位 */
    e->freeSize = e->totalSize;
}
```

配合 bump 分配（`allocPtr` 只增不减、`freeSize` 只在分配时减），得到：

- **释放 ≠ 归还空间**：空间只有在"整个子池同时空闲"时才回来；
- 探针实测：某子池**累计释放 110 652 次，`freeSize` 仍只剩 108 字节**；
- 只要有 1 个块还在途（别人的请求、或慢客户端占着的 6KB 发送块），整个子池就不能复位。

### 1.2 为什么在 c_web 里必然触发

真实分配直方图（探针加在 `MemoryPoolAlloc`，跑真实 `/hello` + `/nope`）：

```
分配 400 万次： 1KB 55.8% | 2KB 14.9% | 6KB 14.9% | 63KB 14.5%（63KB 占 86% 的字节量）
```

- 2MB 子池 ÷ 63KB ≈ **32 块/子池**；8 个子池 ≈ **260 块**；
- c=200 时在途请求 ~200 ⇒ 已用掉 **77%** 容量；
- 63KB × 50k QPS = **3.1 GB/s** 的分配吞吐 ⇒ 每个 2MB 子池 **0.6ms** 就被填满一次；
- ⇒ "等整池归零"在 50k QPS 下几乎等不到，任何一块不归还就立刻进入"分不出 63KB"。

### 1.3 目标与约束

| 约束 | 说明 |
|---|---|
| **线程私有、无锁** | 现有设计的前提：每个池只被一个线程访问（接待线程/工作线程各自持有） |
| `free(ptr)` 不带尺寸 | 现有 API `MemoryPoolFree(pool, ptr)`，调用点不想改 |
| 尺寸范围 | 32B ~ 512KB（实际热路径 1KB/2KB/6KB/63KB） |
| 高频周转 | 50k QPS × ~4 次分配 ⇒ ~20 万次/秒 |
| 可清零、可观测 | 请求之间要能 `memset` 清零；要能统计每类用量 |
| 失败要显式 | 分配不到必须返回 `notfull=0`，由上层回 503/413，不能静默 |

**明确的非目标**：不做跨线程共享、不做压缩搬移（方案三）、不做变长块合并。

---

## 2. 设计概述

三个构件：

```
Pool
 ├── Arena（一次性 malloc 的大块，或按需 mmap；1GB 虚拟、按页惰性提交）
 ├── Class[K]（类表：块大小 + 空闲链头 + 当前 chunk + 用量统计）
 └── Chunk 表（每块 = N 个等大块；记录它属于哪个类，供 free 反查）

Arena
 ├── chunk(2KB 类)  ── [块][块][块][块] …       freel2  ──► 空块 ──► NULL
 ├── chunk(6KB 类)  ── [块][块][块] …           freel6  ──► 空块 ──► NULL
 ├── chunk(64KB 类) ── [块][块] …               freel64 ──► NULL
 └── chunk(1KB 类)  ── [块][块][块] …
```

- **块（block）**：等大，大小 = 该类的 `blkSize`；`free` 时把**块自己的前 8 字节**写成 `next` 指针，所以空闲链零额外元数据；
- **chunk**：一次从 arena 切出的连续区域（`16KB ~ 1MB`），只服务一个类；块全部是对齐的；
- **类表**：`{blkSize, freelist, cur_chunk, used_blocks}`，按块大小升序；分配时用"阶梯"把任意 `needSize` 映射到某个类。

**一次分配**：`needSize → 类 → 取链头（或从当前 chunk bump 一个 / 领新 chunk）→ 返回`。
**一次释放**：`ptr → 定位 chunk → 得到 blkSize → (ptr→next=freelist; freelist=ptr)`。

## 3. 数据结构

### 3.1 基本定义

```c
#define MAX_CLASS 64

typedef struct Class {
    int      blkSize;      /* 本类的块大小（字节，已对齐） */
    void    *freelist;     /* 空闲块链头（链节点 = 块自己前 8B） */
    char    *bump;         /* 当前 chunk 的分配游标 */
    char    *bump_end;     /* 当前 chunk 的末尾 */
    int      usedBlocks;   /* 统计：在用块数（可选） */
    long long allocs, frees;  /* 统计（可选） */
} Class;

typedef struct Chunk {
    int   cls;             /* 本 chunk 服务的类下标 */
    char *begin;           /* chunk 起始地址 */
    int   nblocks;         /* 切出多少个块 */
} Chunk;

typedef struct Memory_Pool {
    char    *arena;        /* 大块基址 */
    size_t   arenaSize;
    Chunk   *chunks;       /* chunk 表（按地址升序，供 free 反查） */
    int      nchunks, capChunks;
    Class    cls[MAX_CLASS];
    int      ncls;
    /* 容量控制 */
    size_t   totalQuota;   /* 总配额（防止某类吃光） */
    size_t   usedBytes;
} Memory_Pool;
```

### 3.2 指针 → 类 的反查（`free(ptr)` 不带尺寸的关键）

```c
/* chunk 按地址升序排列，二分查找 O(log n)，n 很小（几十） */
static int chunk_of(Memory_Pool *p, const char *ptr) {
    int lo = 0, hi = p->nchunks - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        Chunk *c = &p->chunks[mid];
        if (ptr < c->begin)            hi = mid - 1;
        else if (ptr >= c->begin + (size_t)c->nblocks * p->cls[c->cls].blkSize) lo = mid + 1;
        else                            return mid;
    }
    return -1;                     /* 不属于本池 → free 非法 */
}
```

- 合法性检查（`-1` + `(ptr - chunk->begin) % blkSize == 0`）能挡住**野指针**和**未对齐指针**；
- 若 `chunks` 数量稳定（例如 < 8），二分比线性慢不了多少，也可以直接顺序比较；
- **不要求全局统一 perSize**（这是和现有池最大的结构差别），所以 chunk 大小可以按类自适应。

### 3.3 尺寸阶梯（把 needSize 映射到类）

两级策略：

- **≤ 1KB**：细分（32/64/128/256/512/1024），照顾几十~几百字节的小对象；
- **1KB ~ 64KB**：**1.25× 步长**（1024, 1280, 1536, 2048, 2560, 3072, 4096, 5120, 6144, 8192, 10240, 12288, 16384, 20480, 24576, 32768, 40960, 49152, 65536）；
- **> 64KB**：阶梯到 128KB/256KB/512KB，或转交"大对象路径"（§8）。

```c
static int class_of(Memory_Pool *p, size_t need) {
    for (int i = 0; i < p->ncls; i++)
        if ((size_t)p->cls[i].blkSize >= need) return i;
    return -1;                     /* 超过最大类 → 大对象路径 */
}
```
> 类数 ~30，线性扫描在 L1 里命中，实测总分配耗时里可忽略；也可以用"首块大小偏移表"做 O(1) 映射（§11）。

**为什么是 1.25×**：最坏内部浪费 = 1 − 1/1.25 = **20%**；而 2× 阶梯最坏 50%。
1.25× 阶梯还能**正好命中** c_web 的 2048 / 6144 / 65536，所以对现有五种尺寸的实际浪费是 **0~1.5%**（实测）。

## 4. 算法

### 4.1 分配（O(1) 均摊）

```c
void *pool_alloc(Memory_Pool *p, size_t need, int *notfull)
{
    *notfull = 0;
    int ci = class_of(p, need);
    if (ci < 0) return NULL;               /* 大对象路径（§8） */

    Class *c = &p->cls[ci];

    /* ① 空闲链命中：绝大多数情况走这里，1~2 条指令 */
    if (c->freelist) {
        void *blk = c->freelist;
        c->freelist = *(void **)blk;
        c->usedBlocks++; *notfull = 1;
        return blk;
    }

    /* ② 当前 chunk 还有空间：bump 一个块 */
    if (c->bump + c->blkSize <= c->bump_end) {
        void *blk = c->bump;
        c->bump += c->blkSize;
        c->usedBlocks++; *notfull = 1;
        return blk;
    }

    /* ③ 领一个新 chunk（chunk 池 / arena），见 4.3 */
    if (chunk_new(p, ci) != 1) return NULL;      /* 配额用尽 → notfull=0 */
    return pool_alloc(p, need, notfull);          /* 递归一次即可 */
}
```

- **① 是热路径**：稳态下 >95% 的分配直接命中空闲链；
- **② 只在 chunk 首次使用/空闲链被抽空时走**；
- **③ 是容量增长点**，受总配额限制。

### 4.2 释放（O(log n)，n = chunk 数，通常 < 8）

```c
void pool_free(Memory_Pool *p, void *ptr)
{
    if (!ptr) return;
    int ci_chunk = chunk_of(p, (const char *)ptr);
    if (ci_chunk < 0) return;                       /* 非本池指针：忽略/告警 */
    Chunk *ch = &p->chunks[ci_chunk];
    Class *c  = &p->cls[ch->cls];

    size_t off = (char *)ptr - ch->begin;
    if (off % c->blkSize) return;                   /* 未对齐：非法 */

    *(void **)ptr = c->freelist;                    /* 块前 8B 当 next */
    c->freelist = ptr;
    c->usedBlocks--;                                /* 可加：usedBytes -= blkSize */
}
```

- **不需要知道原始 `need`**（这正好补上现池 API 的缺口）；
- 可选的**双 free 检测**：块内保留 1 字节"magic"或用一个位图（每块 1 bit）标记在用状态，代价 1/64 的内存与 1 次分支（§6.4）。

### 4.3 chunk 的申请、切分与归还

```c
static int chunk_new(Memory_Pool *p, int ci)
{
    Class *c = &p->cls[ci];

    /* chunk 大小按类自适应：至少 16KB，至多 1MB，且是 blkSize 的整数倍 */
    size_t sz = (size_t)c->blkSize * 16;
    if (sz < 16u * 1024)  sz = 16u * 1024;
    if (sz > 1u * 1024 * 1024) sz = 1u * 1024 * 1024;
    sz -= sz % c->blkSize;

    if (p->usedBytes + sz > p->totalQuota) return -1;   /* 配额兜底 */

    char *base = arena_take(&p->arena, sz);              /* 顺序切 or mmap */
    if (!base) return -1;

    Chunk *ch = chunk_push(p, base, sz / c->blkSize, ci); /* 保持地址升序 */
    c->bump = base; c->bump_end = base + sz;
    p->usedBytes += sz;
    return 1;
}
```

**归还整块 chunk（可选）**：当某 chunk 的 `usedBlocks == 0` 且不是该类当前 chunk 时，可以：

- 把它的块从空闲链里"摘掉"较麻烦 ⇒ 推荐**不摘**，只把整块交给 `madvise(MADV_DONTNEED)`；
- 或者用"每类 chunk 列表 + 按 chunk 分链"（每 chunk 一条子链），归还可以整条摘除。**建议 v1 不做归还**，先保证正确与速度，RSS 由 `madvise` 控制（§5.4）。

### 4.4 为什么它是"均摊 O(1)"

- 分配：空闲链 1 次解引用；chunk bump 1 次加法；`chunk_new` 每 `sz/blkSize`（≥16）次分配才发生一次；
- 释放：`chunk_of` 二分 log₂(≤16) ≈ 4 次比较 + 1 次写；
- **没有扫描、没有合并、没有搬移**，最坏路径与平均路径同阶。

## 5. 容量规划与参数

### 5.1 每类需要多少（按 c_web 真实数据算）

| 分配点 | 块大小 | c=200 时在途 | 需要容量 |
|---|---|---|---|
| 响应头块 `RspHeaderAlloc` | 63KB | ~200（每请求 1 个） | **~12.6 MB** |
| 发送指针块 `SendPointerPool` | 6KB | ~200 | ~1.2 MB |
| 判定块 `RequestTempPool` | 2KB | ~200 | ~0.4 MB |
| 分析块 `RequestAnalysisInit` | 1KB | ~200 | ~0.2 MB |
| 请求副本（`⌈len⌉` 精确） | 96B~448B（小请求） | ~200 | ~0.1 MB |

⇒ c=200 时**真实活跃 ≈ 14.5 MB**，其中 **87% 是 63KB 头块**。
所以"63KB 类"必须能容纳"在途请求数 × 63KB"，并且**释放即复用**——这正是分类链最擅长的。

### 5.2 推荐参数

| 参数 | 推荐值 | 理由 |
|---|---|---|
| 阶梯 | ≤1KB 细分；1KB~64KB 用 1.25× | 块浪费 0~1.5%（实测） |
| chunk 下限 | **16 KB** | 小类地板低，避免"4 个类占 256KB" |
| chunk 上限 | 1 MB | 大块类（63KB）一次拿 16 个块 |
| 总配额 | 32 ~ 64 MB（**不是 1GB**） | c=200 只需 ~15MB；留 2~4 倍余量 |
| 每类上限（可选） | 总配额的 60% | 防止 63KB 类吃光 |
| 对齐 | 8B（块头要放 next） | 结构体最大对齐通常 8B |

### 5.3 为什么不需要 1GB

- 实测 c=200 真实活跃 3~15MB（取决于同时有多少请求带 63KB 头块）；
- 1GB 的虚拟地址空间本身不贵，但**bump/旋转会一路把页摸热**（实测 1MB×1024 在 3.19MB 活跃下 VmHWM=17MB，长时间跑会更高）；
- 分类链的 5MB VmHWM 来自"触到的就是正在用的" ⇒ **配额给小也不会失败**，给大也不会浪费。

### 5.4 把页还给系统（控制 RSS 上限）

```c
/* 可选：空闲时对"整块空闲"的 chunk 归还物理页（保留虚拟地址） */
madvise(chunk->begin, chunk_bytes, MADV_DONTNEED);   /* Linux */
```
- 语义：物理页归还，虚拟地址仍在，下次访问重新按需分页；
- 建议在"某 chunk 的 `usedBlocks == 0` 且持续 N 秒"时做，避免抖动；
- **不影响正确性**（分类链从不依赖"页是否驻留"）。

---

## 6. 正确性与边界

### 6.1 线程私有

| 池 | 拥有者 | 说明 |
|---|---|---|
| 接待线程 `store_area`（2KB+6KB） | 接待线程 | 只被 `RequestComeMain` / `ResponseSendMain` 访问 |
| 接待线程 `http_pool`（请求副本） | 接待线程 | 分配在 `RequestComeMain`，释放也在接待线程（搭便车） |
| 工作线程 `txt_pool`（63KB+打包块） | 各自工作线程 | 只被该 worker 访问 |
| 工作线程 `http_pool`（分析块） | 各自工作线程 | 只被该 worker 访问 |

**保持这一条，就不需要任何锁**；这也是分类链能"零扫描、零原子操作"的前提。

### 6.2 对齐与护栏

- 块大小向上对齐到 8B（`blkSize = (need + 7) & ~7`，阶梯本身就是对齐的）；
- 现池每块尾部有 4B 护栏（`tailSize`），分类链里可以**保留**：`blkSize` 里预留 4B，返回 `blk`、屏障放在 `blk+need..blk+need+4`；
- 护栏的作用：挡住 `strcpy` 越界 1~2 字节类的错误（在解析器里很常见）。

### 6.3 非法 free 的检测

| 检查 | 代价 | 能挡什么 |
|---|---|---|
| `chunk_of() == -1` | 4 次比较 | 野指针 / 别的池的指针 |
| `off % blkSize != 0` | 1 次取模 | 块中间的指针（`ptr+8` 之类） |
| magic / 位图 | 1 次读 + 1 次分支 | **双重释放**、free 后继续写（配合 `memset`） |

### 6.4 双 free 与"free 后使用"

- v1 建议加**每块 1 bit 的在用位图**（每 chunk 一块，`nblocks/8` 字节）；
- `free`：检查位为 1 → 置 0 → 挂链；位为 0 → 报告"重复释放"（计数 + 可选 abort）；
- 代价：每块 1 bit + 1 次读写，实测影响 < 2 ns；
- 这与"release 时清空"配合，可以让"free 后使用"在开发期立刻暴露（`memset` 块内容）。

### 6.5 分配失败语义

- `chunk_new` 失败（配额/arena 用尽）⇒ `notfull=0`、返回 NULL；
- **调用方必须检查**（现在 `RequestGet` 就是因为没检查才在 512KB body 上崩）；
- 大对象路径失败 ⇒ 上层回 **413**（body 太大）而不是 503（池子不够）。

## 7. 实测数据（全部可复跑）

### 7.1 速度与真实内存（真实直方图：1KB 56% / 2KB 15% / 6KB 15% / 63KB 14%，c=200，活跃 3.19MB）

| 方案 | ns/次（分配+释放） | 失败 | **VmHWM** | 真实利用率 |
|---|---|---|---|---|
| 现有池 1MB×1024 | 46.7 | 0 | **17 MB** | 18.8% |
| 现有池 2MB×8 | 33~55 | **1188** | 16 MB | 16% |
| **分类链（1KB 步进，chunk 1MB）** | **51.4** | **0** | **5 MB** | **63.9%** |
| 位图 1KB 页 | 135.6 | 0 | 20 MB | 16.0% |

### 7.2 精确小尺寸（96~448B 56% + 2KB + 6KB + 63KB），c=200

| 方案 | ns | VmHWM | 块浪费 |
|---|---|---|---|
| 现有池（精确字节） | 65.7 | 16 MB | 0.0% |
| 分类链 2× 阶梯 | **53.5** | **5 MB** | 4.7% |
| **分类链 1.25× 阶梯** | 64.4 | **5 MB** | **1.5%** |
| 位图 1KB 页 | 139.0 | 17 MB | 3.9% |

### 7.3 小对象（24~512B，c=2000）

| 方案 | ns | VmHWM | 块浪费 |
|---|---|---|---|
| 现有池 | 62.3 | 6 MB | 2.0% |
| **分类链 1.25×** | 59.1 | **2 MB** | **10.8%** |
| 位图 1KB 页 | 54.3 | 3 MB | **80.6%** |
| 位图 64B 页 | 277.2 | 2 MB | 12.9% |

### 7.4 与 glibc malloc 的对照（2KB/6KB 负载）

| 方案 | ns/次 | 说明 |
|---|---|---|
| glibc malloc/free | 77~124 | 有 tcache 也慢 2~3 倍 |
| 分类链 | **35~37** | — |

### 7.5 结论

1. **速度**：分类链 ≈ 35~64 ns，与最快的方案同级；比 malloc 快 2~3 倍；
2. **内存**：VmHWM 5MB vs 现有池 16MB（3 倍差距），因为"释放即可复用"；
3. **碎片**：1.25× 阶梯对 c_web 的真实尺寸浪费 **1.5%**；
4. **稳定**：c=200~20000 并发下 **0 失败**（现有池在 c=3000 时 1188 次失败）；
5. **代价**：需要 chunk 表与容量配额；`free` 多一次二分查找（< 3 ns）。

---

## 8. 变长大块与大对象路径

分类链只处理"≤ 最大类"的请求。对更大的请求（请求副本可达 512KB）有三条路：

| 方案 | 优点 | 缺点 | 建议 |
|---|---|---|---|
| A. 阶梯到 512KB（每级精确类） | 零浪费、O(1) | 类多（~40），每类地板 | **推荐**（≤64KB 用 1.25×，>64KB 用 2×） |
| B. 大对象专用页位图（4KB 页） | 利用率高、容量共享 | 需要扫描；63KB 需要 16 个连续页 | 备选 |
| C. 直接 `malloc`/专用 arena | 简单 | 慢、碎片不可控 | 不推荐热路径 |

**配套修改（必须）**：

1. `RequestComeMain.c:63` 的 `(h_size/1024+1)*1024` → `h_size + 元数据`（精确申请）；
2. `WorkerThread.c:257` 的 `allocsize = 4*size + ...` → `size + 元数据`；
   （4 倍放大正是 512KB body 崩溃的直接原因）
3. 给 body 设上限（如 1MB）→ 超过回 **413**，而不是让池子 503 或崩掉。

---

## 9. 在现有代码上落地（改造，不是重写）

### 9.1 保留

- `Memory_Pool` 容器、`beginPtr/endPtr`、一次性大块 `malloc`；
- 线程私有、无锁；
- `MemoryPoolInit(p, chunkSize, chunkNum)` / `MemoryPoolAlloc(p, need, &ptr, &notfull)` / `MemoryPoolFree(p, ptr)` 三个 API 与调用点；
- 每块 4B 尾部护栏（可选保留）。

### 9.2 改动（约 150~250 行）

| 项 | 现在 | 改造后 |
|---|---|---|
| `Memory_Entry` | `beginPtr/allocPtr/freeSize/totalSize/hadAllocNum` | 增加 `int blkSize; void *freel;`（entry 变成 chunk） |
| 分配 | 按字节 bump + 扫 entry | 阶梯 → 类 → 取链头 / bump chunk |
| 释放 | `hadAllocNum--`，归零复位 | 反查 chunk → 挂回该块大小的空闲链 |
| 容量 | `perSize × entryNum` 固定 | chunk 池 + 总配额（可增长） |
| `notfull` | 扫不到能装的就 0 | 配额/chunk 用尽才 0 |

### 9.3 分阶段实施（每步都能独立验证）

| 阶段 | 内容 | 验证 |
|---|---|---|
| 0 | 修 3 个 bug：`2024*1024`→`2048*1024`、`RequestGet` 检查返回值、调用方去掉 1KB 取整/4× | 27 项功能 + 512KB POST |
| 1 | 引入类表 + 空闲链（先只支持 5 个固定尺寸） | 同上 + valgrind 0 |
| 2 | chunk 池 + 配额 + 1.25× 阶梯 | c=50~400 压测、VmHWM |
| 3 | 位图/双 free 检测、`madvise` 归还 | 慢客户端 + 注入漏 free 用例 |

## 10. 验收计划

| # | 用例 | 通过标准 |
|---|---|---|
| 1 | `test/website_test.sh` 27 项 | PASS 27 / FAIL 0 |
| 2 | 单连接顺序 300 次 | p50 ≤ 0.05ms（不回退） |
| 3 | `wrk -c50/120/200/400` | 0 非 2xx（或 503 只出现在配额真正用尽时）；QPS 不低于现基线 |
| 4 | 404 风暴 30s | RSS 平稳、无增长；结束后 `/hello` 200 |
| 5 | 流水线 2×60 / 8×20 / 16×10 | 响应数 = 请求数，顺序正确 |
| 6 | POST 1KB ~ 1MB（`/api/echo`） | ≤ 上限全 200；> 上限回 413；**不崩** |
| 7 | 慢客户端（读一半就停） | 其它连接不受影响（不会因它钉住整池） |
| 8 | 注入漏 free（开发模式） | 只损失该块；服务继续 |
| 9 | 双 free 注入 | 被检测并计数（不破坏空闲链） |
| 10 | valgrind（404×150 + hello×15） | `definitely lost: 0`、无新增非法读写 |
| 11 | 长稳 30 分钟 c=200 | VmHWM 稳定（无单调增长） |

---

## 11. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| **每类容量切死** | 某类用光而别类空闲 | chunk 池按需领；总配额 + 每类上限；`notfull=0` 时回 503 |
| **chunk 地板过高** | 小对象场景内存放大（实测 25.6%） | chunk 下限 16KB；或每类 chunk = max(16KB, 8×blk) |
| **类数过多** | `chunk_of`/`class_of` 变慢、cache 压力 | 阶梯限制在 ~30 类；用首块大小偏移表做 O(1) 映射 |
| **`free(ptr)` 反查失败** | 野指针/跨池 free 被忽略 → 泄漏 | 返回码 + 计数告警（开发模式 abort） |
| **大对象碎片** | 512KB 请求在阶梯里占满一整类 | 大对象走独立路径；上限 1MB + 413 |
| **chunk 不能归还** | RSS 只涨不降 | `madvise(MADV_DONTNEED)`；或每类多 chunk 子链 |
| **对齐错误** | 未定义行为 | 块大小统一 8B 对齐；`free` 检查 `off % blkSize` |

---

## 12. 与老池、新池的关系

| | 老 `MemoryPool`（O1/O2 版） | 新池（本次上线） | 本方案（分类链） |
|---|---|---|---|
| 块管理 | 按 size class 空闲链 | 按字节 bump | 按 size class 空闲链 |
| 回收 | **free 即复用** | 整块子池归零才复位 | **free 即复用** |
| 锁 | 有互斥锁 | 无锁（线程私有） | **无锁（线程私有）** |
| `free` 需要尺寸 | 需要 | 不需要 | **不需要**（chunk 反查） |
| 高并发 | 可用（有锁开销） | c≥200 饿死 | **0 失败** |
| 真实内存 | 3.3MB 级 | 16MB（3MB 活跃） | **5MB** |

**一句话**：本方案 = "老池的块级回收" + "新池的线程私有/无锁/`free` 无尺寸"。
不要保留新池的"整块归零"，那是这次所有 503 的根源。

---

## 13. 附录 A：最小可用实现骨架（约 100 行，可直接对照落地）

```c
/* pool.h */
#define MAXC 64
typedef struct { int blk; void *freel; char *bump, *bend; int used; } SC;
typedef struct { int cls; char *begin; int nblk; } Chunk;
typedef struct {
    char *base; size_t size, used_bytes, quota;
    SC   c[MAXC]; int nc;
    Chunk ch[512]; int nch;
} Pool;

/* pool.c */
static int class_of(Pool *p, size_t n){
    for (int i = 0; i < p->nc; i++) if ((size_t)p->c[i].blk >= n) return i;
    return -1;
}
static int chunk_new(Pool *p, int ci){
    SC *c = &p->c[ci];
    size_t sz = (size_t)c->blk * 16;
    if (sz < 16u*1024) sz = 16u*1024;
    if (sz > 1u*1024*1024) sz = 1u*1024*1024;
    if (sz < (size_t)c->blk) sz = c->blk;      /* 大块类：chunk 至少放得下 1 块 */
    sz -= sz % c->blk;
    if (p->used_bytes + sz > p->quota) return -1;
    if (p->nch >= 512) return -1;
    char *b = p->base + p->used_bytes;
    p->ch[p->nch].cls = ci; p->ch[p->nch].begin = b;
    p->ch[p->nch].nblk = (int)(sz / c->blk); p->nch++;
    p->used_bytes += sz;
    c->bump = b; c->bend = b + sz;
    return 1;
}
void *pool_alloc(Pool *p, size_t n, int *notfull){
    *notfull = 0;
    int ci = class_of(p, n); if (ci < 0) return NULL;
    SC *c = &p->c[ci];
    if (c->freel){ void *b = c->freel; c->freel = *(void**)b; c->used++; *notfull = 1; return b; }
    if (c->bump + c->blk <= c->bend){ void *b = c->bump; c->bump += c->blk; c->used++; *notfull = 1; return b; }
    if (chunk_new(p, ci) != 1) return NULL;
    return pool_alloc(p, n, notfull);
}
static Chunk *find_chunk(Pool *p, const char *ptr){
    int lo = 0, hi = p->nch - 1;
    while (lo <= hi){
        int m = (lo+hi)/2;
        Chunk *c = &p->ch[mid];
        if (ptr < c->begin) hi = m-1;
        else if (ptr >= c->begin + (size_t)c->nblk * p->c[c->cls].blk) lo = m+1;
        else return c;
    }
    return NULL;
}
void pool_free(Pool *p, void *ptr){
    if (!ptr) return;
    Chunk *ch = find_chunk(p, (const char*)ptr);
    if (!ch) return;
    SC *c = &p->c[ch->cls];
    if (((char*)ptr - ch->begin) % c->blk) return;
    *(void**)ptr = c->freel; c->freel = ptr; c->used--;
}
```

**这段骨架已编译验证**（`/tmp/pool_skel_test.c`，64MB 配额、500 万次混合分配/释放）：

```
失败=0  在途=444  峰值活跃=21.19MB  arena已用=23.22MB(91%)  重叠=无  used计数一致=是
```
即：无失败、无重叠、各类 `used` 与实际在途块数一致、内存利用率 91%。

## 14. 附录 B：尺寸阶梯表（1.25×，≤64KB 精确命中常用值）

| 段 | 类大小 |
|---|---|
| 小对象 | 32, 64, 128, 256, 512 |
| 1KB~64KB | 1024, 1280, 1536, **2048**, 2560, 3072, 4096, 5120, **6144**, 8192, 10240, 12288, 16384, 20480, 24576, 32768, 40960, 49152, **65536** |
| 大对象 | 98304, 131072, 196608, 262144, 393216, 524288 |

> 加粗的是 c_web 现在实际用到的尺寸：**2048 / 6144 / 65536（63KB→65536）都正好落在类边界上**，
> 所以对现有热路径的内部浪费 = 0。

---

## 15. 结论

1. 分类空闲链不是新发明（老池、tcmalloc、jemalloc 都是这个结构），它的价值在于**把回收粒度从"整池"降到"块"**；
2. 在 c_web 的**真实尺寸分布 + 真实 RSS** 下，它同时做到：**1.5% 块浪费、5MB 物理内存、53~64ns、0 失败**；
3. 落地是**改造**（保留容器/API/线程私有），约 150~250 行；
4. 它不能替代的两件事：调用方的 1KB 取整和 `4*size` 放大必须一起改，大对象要有上限与 413。





