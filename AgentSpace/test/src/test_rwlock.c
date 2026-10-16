#include "../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static my_rwlock_t g_rw;
static pthread_mutex_t g_stat = PTHREAD_MUTEX_INITIALIZER;
static int g_active = 0;
static int g_max_active = 0;
static int g_w1_in = 0;

static char g_log[4096];
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_event(const char* s)
{
    pthread_mutex_lock(&g_log_lock);
    size_t len = strlen(g_log);
    if (len + strlen(s) + 2 < sizeof(g_log)) { strcpy(g_log + len, s); strcat(g_log, "|"); }
    pthread_mutex_unlock(&g_log_lock);
}
static void enter_cs(void)
{
    pthread_mutex_lock(&g_stat);
    g_active++;
    if (g_active > g_max_active) g_max_active = g_active;
    pthread_mutex_unlock(&g_stat);
}
static void leave_cs(void)
{
    pthread_mutex_lock(&g_stat);
    g_active--;
    pthread_mutex_unlock(&g_stat);
}
static void reset_stat(void)
{
    pthread_mutex_lock(&g_stat);
    g_active = 0;
    g_max_active = 0;
    pthread_mutex_unlock(&g_stat);
}

//读线程：多个读者应能同时进入临界区
static void* reader_task(void* a)
{
    (void)a;
    my_lock_rdlock(&g_rw);
    enter_cs();
    log_event("R-enter");
    usleep(150000);
    log_event("R-exit");
    leave_cs();
    my_lock_unlock(&g_rw);
    return NULL;
}

//写线程：写者之间必须互斥
static void* writer_task(void* a)
{
    (void)a;
    my_lock_wrlock(&g_rw);
    enter_cs();
    usleep(50000);
    leave_cs();
    my_lock_unlock(&g_rw);
    return NULL;
}

//W1：先持有写锁 200ms
static void* writer_holder_task(void* a)
{
    (void)a;
    my_lock_wrlock(&g_rw);
    pthread_mutex_lock(&g_stat);
    g_w1_in = 1;
    pthread_mutex_unlock(&g_stat);
    log_event("W1-enter");
    usleep(200000);
    log_event("W1-exit");
    my_lock_unlock(&g_rw);
    return NULL;
}

//W2：排在写者队列中，应优先于之后到来的读者
static void* writer_waiter_task(void* a)
{
    (void)a;
    usleep(50000); //确保 W1 已持有写锁
    my_lock_wrlock(&g_rw);
    log_event("W2-enter");
    usleep(50000);
    log_event("W2-exit");
    my_lock_unlock(&g_rw);
    return NULL;
}

int main(void)
{
    printf("== test_rwlock : 写优先读写锁 ==\n");

    CHECK(my_rwlock_init(&g_rw) == 1, "my_rwlock_init 成功");
    CHECK(g_rw.readers == 0 && g_rw.writers == 0 && g_rw.waiting_writers == 0, "初始计数全为 0");

    //1.单线程加解锁
    my_lock_rdlock(&g_rw);
    CHECK(g_rw.readers == 1, "单线程加读锁后 readers=1");
    my_lock_unlock(&g_rw);
    CHECK(g_rw.readers == 0, "释放读锁后 readers=0");
    my_lock_wrlock(&g_rw);
    CHECK(g_rw.writers == 1, "单线程加写锁后 writers=1");
    my_lock_unlock(&g_rw);
    CHECK(g_rw.writers == 0, "释放写锁后 writers=0");

    //2.并发读：4 个读者同时进入
    reset_stat();
    g_log[0] = '\0';
    pthread_t th[4];
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, reader_task, NULL);
    usleep(80000);
    pthread_mutex_lock(&g_stat);
    int read_peak = g_max_active;
    pthread_mutex_unlock(&g_stat);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    CHECK(read_peak >= 2, "读锁共享：≥2 个读者同时处于临界区");
    CHECK(g_rw.readers == 0, "全部释放后 readers 归零");

    //3.写互斥：4 个写者顺序进入，同一时刻最多 1 个
    reset_stat();
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, writer_task, NULL);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    CHECK(g_max_active == 1, "写锁互斥：同一时刻只有 1 个写者");
    CHECK(g_rw.writers == 0 && g_rw.waiting_writers == 0, "写者全部释放后计数归零");

    //4.写优先：排队中的写者优先于后来的读者
    g_log[0] = '\0';
    g_w1_in = 0;
    reset_stat();
    pthread_t t_holder, t_waiter;
    pthread_create(&t_holder, NULL, writer_holder_task, NULL);
    for (int i = 0; i < 2000; i++) {
        pthread_mutex_lock(&g_stat);
        int in = g_w1_in;
        pthread_mutex_unlock(&g_stat);
        if (in) break;
        usleep(1000);
    }
    pthread_create(&t_waiter, NULL, writer_waiter_task, NULL);
    usleep(60000); //等 W2 进入等待队列（waiting_writers=1）

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    my_lock_rdlock(&g_rw);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    double wait_ms = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_nsec - t1.tv_nsec) / 1e6;
    log_event("R-enter");
    my_lock_unlock(&g_rw);

    pthread_join(t_waiter, NULL);
    pthread_join(t_holder, NULL);

    char* pW1e = strstr(g_log, "W1-enter");
    char* pW1x = strstr(g_log, "W1-exit");
    char* pW2e = strstr(g_log, "W2-enter");
    char* pW2x = strstr(g_log, "W2-exit");
    char* pRe = strstr(g_log, "R-enter");
    int order_ok = pW1e && pW1x && pW2e && pW2x && pRe &&
                   pW1e < pW1x && pW1x < pW2e && pW2e < pW2x && pW2x < pRe;
    CHECK(order_ok, "事件顺序 W1-enter < W1-exit < W2-enter < W2-exit < R-enter");
    CHECK(wait_ms >= 100.0, "读者被排队的写者阻塞（等待 ≥100ms）");
    printf("  (读者实际等待 %.1f ms)\n", wait_ms);

    printf("---- test_rwlock: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
