#include "../../../Include/Include.h"

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

//---------------- WorkerQueue（响应内存归还队列）----------------
static Worker_Queue* g_wq;
static int g_wq_err = -1;
static int g_wq_size = -1;
static double g_wq_ms = 0;
static int g_wq_push_ret = 0;

static void* wq_pending_pop(void* a)
{
    (void)a;
    Worker_Queue_Entry en = WorkerQueueTopAndPop(g_wq, &g_wq_err);
    g_wq_size = en.size;
    g_wq_ms = now_ms();
    return NULL;
}
static void* wq_pending_push(void* a)
{
    (void)a;
    g_wq_push_ret = WorkerQueuePush(g_wq, 99, (char*)0x99);
    return NULL;
}

//---------------- TaskQueue（全局任务队列）----------------
static Task_Queue* g_tq;
static int g_tq_fd = -1;
static int g_tq_size = -1;
static double g_tq_ms = 0;

static void* tq_pending_pop(void* a)
{
    (void)a;
    int err = 0;
    Task_Queue_Entry t = TasKQueueTopAndPop(g_tq, &err);
    g_tq_fd = t.fd;
    g_tq_size = t.h_size;
    g_tq_ms = now_ms();
    return NULL;
}

int main(void)
{
    printf("== test_queues : 三个队列 FIFO / 环绕 / 非阻塞背压 ==\n");

    //================ 1.WorkerQueue ================
    CHECK(WorkerQueueInit(&g_wq, 3) == 1, "WorkerQueueInit(3) 成功");
    CHECK(g_wq->blocknum == 3 && g_wq->num == 0, "容量 3、初始为空");
    CHECK(WorkerQueuePush(g_wq, 11, (char*)0xA1) == 1, "push 元素1");
    CHECK(WorkerQueuePush(g_wq, 22, (char*)0xA2) == 1, "push 元素2");
    CHECK(g_wq->num == 2, "有效元素数 2");

    int e = -1;
    Worker_Queue_Entry en = WorkerQueueTopAndPop(g_wq, &e);
    CHECK(e == 1 && en.size == 11 && en.char_ptr == (char*)0xA1, "pop 先入先出：取到元素1");
    en = WorkerQueueTopAndPop(g_wq, &e);
    CHECK(e == 1 && en.size == 22, "pop 取到元素2");
    en = WorkerQueueTopAndPop(g_wq, &e);
    CHECK(e == 0, "空队列 pop → error=0（不阻塞）");

    //环绕：push/pop 交叉进行，验证环形指针回绕
    WorkerQueuePush(g_wq, 1, (char*)0x1);
    WorkerQueuePush(g_wq, 2, (char*)0x2);
    WorkerQueueTopAndPop(g_wq, &e);
    WorkerQueuePush(g_wq, 3, (char*)0x3);
    WorkerQueuePush(g_wq, 4, (char*)0x4); //此时写入指针已回绕到 begin
    int wrap_ok = 1;
    en = WorkerQueueTopAndPop(g_wq, &e);
    if (en.size != 2) wrap_ok = 0;
    en = WorkerQueueTopAndPop(g_wq, &e);
    if (en.size != 3) wrap_ok = 0;
    en = WorkerQueueTopAndPop(g_wq, &e);
    if (en.size != 4) wrap_ok = 0;
    CHECK(wrap_ok == 1, "环形回绕后仍按 2,3,4 顺序出队");
    CHECK(g_wq->num == 0, "环绕测试后队列清空");

    //出队无阻塞语义：清理线程从空队列取内存时应立即返回 error=0，不能卡住发送线程
    pthread_t th;
    g_wq_err = -1;
    double t_start = now_ms();
    pthread_create(&th, NULL, wq_pending_pop, NULL);
    pthread_join(th, NULL);
    CHECK(g_wq_err == 0, "空队列 pop 立即返回 error=0（不阻塞）");
    CHECK(now_ms() - t_start < 50.0, "空队列 pop 耗时 <50ms");

    //满队列：push 阻塞直到有元素出队
    Worker_Queue* wq2 = NULL;
    WorkerQueueInit(&wq2, 2);
    WorkerQueuePush(wq2, 1, (char*)0x1);
    WorkerQueuePush(wq2, 2, (char*)0x2); //已满
    g_wq = wq2;
    g_wq_push_ret = -1;
    pthread_create(&th, NULL, wq_pending_push, NULL);
    usleep(80000);
    CHECK(g_wq_push_ret == -1, "队列满时 push 线程阻塞");
    WorkerQueueTopAndPop(wq2, &e);
    pthread_join(th, NULL);
    CHECK(g_wq_push_ret == 1, "出队一个后 push 线程恢复执行");

    //================ 2.TaskQueue（收发线程 → 业务线程）================
    CHECK(TasKQueueInit(&g_tq, 3) == 1, "TasKQueueInit(3) 成功");
    CHECK(TasKQueuePush(g_tq, NULL, 100, 0, 200, (char*)0xB1, 128) == 1, "push 任务1");
    CHECK(TasKQueuePush(g_tq, NULL, 101, 1, 200, (char*)0xB2, 256) == 1, "push 任务2");
    CHECK(g_tq->num == 2, "有效任务数 2");

    Task_Queue_Entry t = TasKQueueTopAndPop(g_tq, &e);
    CHECK(e == 1 && t.fd == 100 && t.serial == 0 && t.h_size == 128, "任务1 FIFO 且字段正确");
    CHECK(t.http == (char*)0xB1 && t.error_reason == 200, "任务1 请求指针/状态码正确");
    t = TasKQueueTopAndPop(g_tq, &e);
    CHECK(e == 1 && t.fd == 101 && t.serial == 1 && t.h_size == 256, "任务2 保序 serial=1");
    CHECK(g_tq->num == 0, "任务队列清空");

    //非阻塞取任务：空队列必须立即返回 error=0。
    //（若业务线程阻塞在空队列上，就无法回收自己内存队列里的块 →
    //  收发线程等空位、业务线程等任务，双方互等 → 这就是原来那道死锁。）
    g_tq_fd = -1;
    g_tq_size = -1;
    double t2 = now_ms();
    pthread_create(&th, NULL, tq_pending_pop, NULL);
    pthread_join(th, NULL);
    CHECK(g_tq_fd == -1 && g_tq_size == 0, "空队列 pop 不阻塞：返回默认值（fd=-1, h_size=0）");
    CHECK(g_tq_ms - t2 < 50.0, "空队列 pop 立即返回（耗时 <50ms）");

    //非阻塞背压：满队列 push 立即返回 0，交回调用者稍后重试（不得阻塞调用线程）
    Task_Queue* tq2 = NULL;
    TasKQueueInit(&tq2, 2);
    CHECK(TasKQueuePush(tq2, NULL, 1, 0, 200, NULL, 0) == 1, "容量 2：push 任务1");
    CHECK(TasKQueuePush(tq2, NULL, 2, 1, 200, NULL, 0) == 1, "容量 2：push 任务2（刚满）");
    CHECK(tq2->num == 2, "满队列时 num==2（计数不得翻倍）");

    double t3 = now_ms();
    int full_ret = TasKQueuePush(tq2, NULL, 3, 2, 200, NULL, 0);
    double full_ms = now_ms() - t3;
    CHECK(full_ret == 0, "队列满时 push 返回 0（非阻塞背压）");
    CHECK(full_ms < 50.0, "满队列 push 立即返回（不阻塞调用线程）");
    CHECK(tq2->num == 2, "被拒绝的 push 不得改动计数");

    t = TasKQueueTopAndPop(tq2, &e);
    CHECK(e == 1 && t.fd == 1, "先入先出：取到 fd=1");
    CHECK(TasKQueuePush(tq2, NULL, 3, 2, 200, NULL, 0) == 1, "消费一个后 push 成功（背压解除）");
    t = TasKQueueTopAndPop(tq2, &e);
    CHECK(e == 1 && t.fd == 2 && t.serial == 1, "剩余任务仍保序（fd=2, serial=1）");
    t = TasKQueueTopAndPop(tq2, &e);
    CHECK(e == 1 && t.fd == 3 && t.serial == 2, "空出的槽位被 fd=3 复用");
    CHECK(tq2->num == 0, "全部取出后 num==0");

    //================ 3.ReceiverQueue（业务线程 → 收发线程）================
    Receiver_Queue* rq = NULL;
    CHECK(ReceiverQueueInit(&rq, 3) == 1, "ReceiverQueueInit(3) 成功");
    CHECK(ReceiverQueuePush(rq, NULL, 5, 0, 200, 512, 0, 10485760, (char*)0xC1, -1, 0) == 1,
          "push 响应事件1（携带 10MB 文件大小）");
    CHECK(ReceiverQueuePush(rq, NULL, 5, 1, 200, 128, 0, 0, (char*)0xC2, -1, 0) == 1, "push 响应事件2");

    Receiver_Queue_Entry r = ReceiverQueueTopAndPop(rq, &e);
    CHECK(e == 1 && r.fd == 5 && r.serial == 0 && r.size_resp == 512, "事件1 FIFO 且字段正确");
    CHECK(r.size_file == 10485760, "off_t 文件大小传递正确（10MB=10485760 字节）");
    CHECK(r.error_reason == 200 && r.char_ptr == (char*)0xC1, "事件1 状态码与内存指针正确");
    r = ReceiverQueueTopAndPop(rq, &e);
    CHECK(e == 1 && r.serial == 1 && r.size_resp == 128, "事件2 保序 serial=1");
    r = ReceiverQueueTopAndPop(rq, &e);
    CHECK(e == 0 && r.fd == -1, "空队列 pop → error=0 且返回默认值");

    printf("---- test_queues: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
