#define _GNU_SOURCE 1
#include "../../Include/Include.h"
#include <sched.h>

/*
 * test_taskqueue_race : 任务队列（CAS 改造版）并发 / 死锁 / 不变式专项
 *
 * 背景：Queue/TaskQueue/TaskQueue.c 被改成 CAS 自旋后，单线程口径"看起来能跑"，
 *       但存在两处结构性隐患，本用例专门把它们逼出来：
 *         (a) CAS 已经把 num ±1 之后，进入互斥区又做了一次 num++/num--，
 *             导致 num 被重复计数（num = 2 × 真实在队数）；
 *         (b) num 走 CAS（无锁），而 ptr_in/ptr_out 走 mutex（有锁），
 *             两者分属不同同步域：CAS 放行了 N 次引用，环形指针才移动 N-1 次，
 *             于是可以"pop 成功但拿到不存在的条目"。
 *
 * 判定手段（不依赖时序猜测，全部是守恒式/边界式检查）：
 *   I1 守恒：每个 push 成功的唯一 serial 必须被取出且仅被取出一次
 *   I2 虚假弹出：队列逻辑为空时 pop 仍返回 error=1
 *   I3 条目完整性：取出的条目必须是某次 push 写入的值（不得是残留/默认值）
 *   I4 计数一致：排空后 num==0；运行中 num 不得高于真实在队数
 *   I5 假满：push 返回 0 而队列实际未满（容量被砍）——严格判定见单线程阶段 A
 *   I6 活性：生产者必须在重试预算内推进完、队列必须在期限内排空（死锁/活锁）
 *   I7 可恢复：压力结束并排空后，队列必须还能继续 push/pop
 *
 * 注意：本文件消费者是"忙轮询"（被测队列已无阻塞语义），因此本用例
 *       同时会把 Worker 线程 100% 空转的活锁问题以 CPU 时间的形式暴露出来。
 */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/*================================ 压力阶段参数 ================================*/
#define QCAP         64              /* 队列容量：远小于压测次数，逼出边界 */
#define NPROD        2               /* 生产者数：对应 2 个收发线程口径 */
#define NCONS        2               /* 消费者数：对应 2 个业务线程口径 */
#define PER_PROD     20000           /* 每个生产者投递次数 */
#define NTOTAL       (NPROD * PER_PROD)
#define RETRY_BUDGET 2000000         /* 单次 push 的重试上限，超出视为永久卡死 */
#define DRAIN_MS     10000.0         /* 排空期限：超过即判定活锁/死锁 */

static Task_Queue*        g_tq;
static atomic_int         g_inflight;   /* 已登记但尚未被取出的元素数（含正在 push 的） */
static atomic_int         g_pushed;     /* push 成功次数 */
static atomic_int         g_popped;     /* pop 成功次数 */
static atomic_int         g_refused;    /* push 返回 0 的次数 */
static atomic_int         g_false_full; /* 假满：返回 0 但实际未满 */
static atomic_int         g_false_pop;  /* 虚假弹出：逻辑空却 pop 成功 */
static atomic_int         g_bad_entry;  /* 取到无效条目 */
static atomic_int         g_dup;        /* 同一条目被取出两次 */
static atomic_int         g_giveup;     /* 生产者耗尽重试预算 */
static atomic_int         g_stop;       /* 消费者退出标志 */
static atomic_uchar       g_seen[NTOTAL];
static pthread_mutex_t    g_seen_lock = PTHREAD_MUTEX_INITIALIZER;

static int join_deadline(pthread_t th, double deadline_ms)
{
    while (now_ms() < deadline_ms) {
        if (pthread_tryjoin_np(th, NULL) == 0) return 0;
        usleep(1000);
    }
    return -1;   /* 到期仍未结束 */
}

/*-------------------------------- 生产者 ------------------------------------*/
typedef struct { int id; } prod_arg_t;

static void* producer(void* a)
{
    prod_arg_t* pa = (prod_arg_t*)a;
    int base = pa->id * PER_PROD;

    for (int k = 0; k < PER_PROD; k++) {
        int serial = base + k;

        /* 登记：先 +1，失败再回退。这样 inflight 永不低报，
           消费者判定"逻辑空却取到东西"时不会误报。 */
        atomic_fetch_add(&g_inflight, 1);

        int r = 0, tries = 0;
        for (;;) {
            r = TasKQueuePush(g_tq, (Receiver_Thread*)0x1, 1000 + serial,
                              serial, 200, (char*)0x5, 64);
            if (r == 1) break;
            if (++tries > RETRY_BUDGET) { atomic_fetch_add(&g_giveup, 1); break; }

            /* 假满取证：返回 0 而逻辑在队数仍小于容量 ⇒ 容量闸门被砍。
               注意只用探针自己的原子计数，不去裸读队列内部的 num，
               否则会与队列锁内的 num 写构成数据竞争（TSAN 会报）。 */
            if (atomic_load(&g_inflight) < g_tq->blocknum)
                atomic_fetch_add(&g_false_full, 1);

            sched_yield();
        }

        if (r == 1) {
            atomic_fetch_add(&g_pushed, 1);
        } else {
            atomic_fetch_sub(&g_inflight, 1);
            atomic_fetch_add(&g_refused, 1);
        }
    }
    return NULL;
}

/*-------------------------------- 消费者 ------------------------------------*/
static void* consumer(void* a)
{
    (void)a;
    while (!atomic_load(&g_stop)) {
        int err = 0;
        Task_Queue_Entry t = TasKQueueTopAndPop(g_tq, &err);
        if (err != 1) { sched_yield(); continue; }

        atomic_fetch_add(&g_popped, 1);

        /* I2：弹出成功却已无元素可弹 → 取到的是不存在的条目 */
        int v = atomic_fetch_sub(&g_inflight, 1);
        if (v <= 0) {
            atomic_fetch_add(&g_inflight, 1);   /* 回退，避免影响后续判定 */
            atomic_fetch_add(&g_false_pop, 1);
        }

        /* I3：条目必须是某次 push 写入的值 */
        if (t.w != (Receiver_Thread*)0x1 || t.serial < 0 || t.serial >= NTOTAL ||
            t.fd != 1000 + t.serial) {
            atomic_fetch_add(&g_bad_entry, 1);
            continue;
        }

        /* I1：同一条目不得被取出两次 */
        pthread_mutex_lock(&g_seen_lock);
        if (g_seen[t.serial]) atomic_fetch_add(&g_dup, 1);
        else                  g_seen[t.serial] = 1;
        pthread_mutex_unlock(&g_seen_lock);
    }
    return NULL;
}

/*================================= 用例主体 =================================*/
int main(void)
{
    printf("== test_taskqueue_race : 任务队列 CAS 版 并发 / 死锁 / 不变式 ==\n");
    printf("  容量 %d，生产者 %d×%d，消费者 %d，排空期限 %.0f ms\n",
           QCAP, NPROD, PER_PROD, NCONS, DRAIN_MS);

    /*================ 阶段 A：单线程确定性（计数与容量）================*/
    Task_Queue* tqa = NULL;
    CHECK(TasKQueueInit(&tqa, 4) == 1, "A. TasKQueueInit(4) 成功");

    CHECK(TasKQueuePush(tqa, (Receiver_Thread*)0x1, 10, 0, 200, NULL, 0) == 1,
          "A. push 第 1 个任务");
    CHECK(tqa->num == 1, "A. push 1 个后 num==1（计数不得翻倍）");

    CHECK(TasKQueuePush(tqa, (Receiver_Thread*)0x1, 11, 1, 200, NULL, 0) == 1,
          "A. push 第 2 个任务");
    CHECK(tqa->num == 2, "A. push 2 个后 num==2");

    int cap_ok = 1;
    for (int i = 2; i < 4; i++)
        if (TasKQueuePush(tqa, (Receiver_Thread*)0x1, 10 + i, i, 200, NULL, 0) != 1)
            cap_ok = 0;
    CHECK(cap_ok == 1, "A. 容量 4 能装下 4 个任务（容量不得被砍半）");

    int ea = 0;
    for (int i = 0; i < 4; i++) TasKQueueTopAndPop(tqa, &ea);
    CHECK(tqa->num == 0, "A. 取空后 num==0");

    /* 闸门位置（确定性）：容量 n 恰好接纳 n 个，第 n+1 个必须被拒且不破坏已有数据 */
    Task_Queue* tqb = NULL;
    TasKQueueInit(&tqb, 3);
    int acc = 0;
    for (int i = 0; i < 3; i++)
        acc += (TasKQueuePush(tqb, (Receiver_Thread*)0x1, 20 + i, i, 200, NULL, 0) == 1);
    CHECK(acc == 3 && tqb->num == 3, "A. 容量 3 恰好接纳 3 个任务");
    CHECK(TasKQueuePush(tqb, (Receiver_Thread*)0x1, 99, 9, 200, NULL, 0) == 0,
          "A. 第 4 个任务被拒（闸门位置正确，非假满）");
    CHECK(tqb->num == 3, "A. 被拒后 num 仍为 3（计数不被污染）");
    int ea2 = 0;
    Task_Queue_Entry ta = TasKQueueTopAndPop(tqb, &ea2);
    CHECK(ea2 == 1 && ta.fd == 20, "A. 取出的仍是第 1 个任务（被拒的插入未覆盖数据）");
    CHECK(TasKQueuePush(tqb, (Receiver_Thread*)0x1, 99, 9, 200, NULL, 0) == 1,
          "A. 让出空位后可入队");

    /*================ 阶段 B：多生产者 / 多消费者压力 ================*/
    CHECK(TasKQueueInit(&g_tq, QCAP) == 1, "B. TasKQueueInit(QCAP) 成功");

    pthread_t   prod[NPROD], cons[NCONS];
    prod_arg_t  pa[NPROD];

    for (int i = 0; i < NCONS; i++) pthread_create(&cons[i], NULL, consumer, NULL);

    double t0 = now_ms();
    for (int i = 0; i < NPROD; i++) {
        pa[i].id = i;
        pthread_create(&prod[i], NULL, producer, &pa[i]);
    }

    int prod_stuck = 0;
    for (int i = 0; i < NPROD; i++)
        if (join_deadline(prod[i], t0 + DRAIN_MS) != 0) prod_stuck++;
    double ms_prod = now_ms() - t0;

    /* 排空：inflight 归零即表示所有登记元素都已被取出 */
    double dl = now_ms() + DRAIN_MS;
    int drained = 0;
    while (now_ms() < dl) {
        if (atomic_load(&g_inflight) <= 0) { drained = 1; break; }
        usleep(1000);
    }
    double ms_all = now_ms() - t0;

    atomic_store(&g_stop, 1);
    for (int i = 0; i < NCONS; i++) join_deadline(cons[i], now_ms() + 5000.0);

    int pushed = atomic_load(&g_pushed);
    int popped = atomic_load(&g_popped);
    int refused = atomic_load(&g_refused);
    int falsefull = atomic_load(&g_false_full);
    int falsepop = atomic_load(&g_false_pop);
    int bad = atomic_load(&g_bad_entry);
    int dup = atomic_load(&g_dup);
    int giveup = atomic_load(&g_giveup);
    int num_end = g_tq->num;
    int inflight = atomic_load(&g_inflight);

    printf("  —— 实测：push 成功 %d / 目标 %d；pop 成功 %d；拒绝 %d；假满 %d；放弃 %d\n",
           pushed, NTOTAL, popped, refused, falsefull, giveup);
    printf("  —— 实测：虚假弹出 %d；无效条目 %d；重复取出 %d；耗时 %.1f ms（生产 %.1f ms）\n",
           falsepop, bad, dup, ms_all, ms_prod);
    printf("  —— 实测：结束时 num=%d（真实在队 %d）、blocknum=%d\n",
           num_end, inflight > 0 ? inflight : 0, g_tq->blocknum);

    /*------------------------- 不变式判定 -------------------------*/
    CHECK(prod_stuck == 0, "I6 生产者全部推进完（未卡死）");
    CHECK(giveup == 0, "I6 未出现耗尽重试预算的永久性拒绝");
    CHECK(drained == 1, "I6 队列在期限内排空（无活锁/死锁）");
    CHECK(falsepop == 0, "I2 无虚假弹出（空队列不会 pop 成功）");
    CHECK(bad == 0, "I3 取出的条目全部来自 push 写入");
    CHECK(dup == 0, "I1 同一条目未被取出两次");
    CHECK(pushed == popped, "I1 push 成功数 == pop 成功数（守恒）");
    CHECK(pushed == NTOTAL, "I5 全部任务最终都被队列接纳（拒绝由重试消化）");
    /* 注：多线程阶段的 falsefull 仅为观测值（见上方实测打印）。push 返回 0 的时刻
       与探针采样 inflight 的时刻之间存在时序间隙，竞争越激烈噪声越大（TSAN 下会被放大）；
       容量闸门的严格判定已放在阶段 A 的确定性检查中，此处不作为判据。 */
    CHECK(num_end == 0, "I4 排空后 num==0（计数无残留）");

    /*================ 阶段 C：压力后的可恢复性 ================*/
    int rec_push = TasKQueuePush(g_tq, (Receiver_Thread*)0x1, 7, 7, 200, NULL, 0);
    int ec = 0;
    Task_Queue_Entry tc = TasKQueueTopAndPop(g_tq, &ec);
    CHECK(rec_push == 1 && ec == 1 && tc.fd == 7,
          "C. 压力后队列仍可正常 push/pop（未留下永久损坏状态）");

    printf("---- test_taskqueue_race: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
