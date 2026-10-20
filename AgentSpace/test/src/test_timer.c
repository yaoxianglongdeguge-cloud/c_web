#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

//测试替身：TimerOvertime 会调用 FdClose 关闭超时连接，这里只记录调用参数
static int g_closed_fd = -1;
static int g_closed_reason = 0;
int FdClose(Receiver_Thread* w, int client_fd, int error_reason)
{
    (void)w;
    g_closed_fd = client_fd;
    g_closed_reason = error_reason;
    return 1;
}

int main(void)
{
    printf("== test_timer : 断连定时器（小顶堆）==\n");

    //------------ 1.小顶堆本身 ------------
    Prior_Queue_Timer* q = NULL;
    CHECK(PriorQueueTimerInit(&q, 8) == 1, "PriorQueueTimerInit(8) 成功");
    CHECK(q->end == 9 && q->elem_end == 1, "容量 9（含哨兵位）、初始空堆");

    Timer_Entry t = PriorQueueTimerTop(q);
    CHECK(t.fd == -2 && t.time == -2, "空堆 Top 返回 fd=-2（无有效条目）");

    CHECK(PriorQueueTimerInsert(q, 101, 1000) == 1, "插入 fd=101 到期时间 1000");
    CHECK(PriorQueueTimerInsert(q, 102, 500) == 1, "插入 fd=102 到期时间 500");
    CHECK(PriorQueueTimerInsert(q, 103, 1500) == 1, "插入 fd=103 到期时间 1500");
    CHECK(q->queue[0].fd == 3, "有效条目计数为 3");

    t = PriorQueueTimerTop(q);
    CHECK(t.fd == 102 && t.time == 500, "堆顶是最小到期时间（fd=102, t=500）");

    CHECK(PriorQueueTimerPop(q) == 1, "弹出堆顶");
    t = PriorQueueTimerTop(q);
    CHECK(t.fd == 101 && t.time == 1000, "弹出后堆顶为 fd=101");
    CHECK(q->queue[0].fd == 2, "计数递减为 2");

    PriorQueueTimerPop(q);
    PriorQueueTimerPop(q);
    t = PriorQueueTimerTop(q);
    CHECK(t.fd == -2, "全部弹出后回到空堆");
    CHECK(q->queue[0].fd == 0, "计数归零");

    //乱序插入 20 个，逐个弹出必须单调不减
    Prior_Queue_Timer* q2 = NULL;
    PriorQueueTimerInit(&q2, 64);
    int insert_ok = 1;
    for (int i = 0; i < 20; i++) {
        if (PriorQueueTimerInsert(q2, 200 + i, 2000 - i * 17) != 1) insert_ok = 0;
    }
    CHECK(insert_ok == 1, "乱序插入 20 个连接");
    int monotonic = 1;
    time_t last = -1;
    int cnt = 0;
    while (q2->queue[0].fd > 0) {
        Timer_Entry top = PriorQueueTimerTop(q2);
        if (last != -1 && top.time < last) monotonic = 0;
        last = top.time;
        cnt++;
        PriorQueueTimerPop(q2);
    }
    CHECK(monotonic == 1 && cnt == 20, "20 个条目按到期时间升序弹出");

    //容量上限：堆满后插入返回 0（上层应扩容）
    Prior_Queue_Timer* q3 = NULL;
    PriorQueueTimerInit(&q3, 3);
    CHECK(PriorQueueTimerInsert(q3, 1, 10) == 1, "容量 3：插入第 1 个");
    CHECK(PriorQueueTimerInsert(q3, 2, 20) == 1, "容量 3：插入第 2 个");
    CHECK(PriorQueueTimerInsert(q3, 3, 30) == 1, "容量 3：插入第 3 个");
    CHECK(PriorQueueTimerInsert(q3, 4, 40) == 0, "堆满 → 插入返回 0");

    //------------ 2.Timer 封装（连接定时器） ------------
    Timer* tm = NULL;
    CHECK(TimerInit(&tm, 8) == 1, "TimerInit(8) 成功");
    CHECK(TimerAlloc(tm, 7, NULL) == 1 && TimerAlloc(tm, 8, NULL) == 1 && TimerAlloc(tm, 9, NULL) == 1,
          "TimerAlloc 登记 3 个连接");
    CHECK(tm->q->elem_end == 4, "堆内 3 个条目");
    CHECK(TimerAlloc(tm, 7, NULL) == 1 && tm->q->elem_end == 4, "重复 TimerAlloc 只重置不新增条目");
    CHECK(TimerReset(tm, 8, NULL) == 1, "TimerReset 重新计时（下滤维持堆序）");
    CHECK(TimerFree(tm, 8) == 1 && tm->q->elem_end == 3, "TimerFree 移除连接 fd=8");
    CHECK(TimerFree(tm, 12345) == 1, "TimerFree 不存在的 fd 安全返回");
    CHECK(tm->q->queue[0].fd == 2, "移除后计数为 2");

    //扩容：容量满时 TimerAlloc 自动扩容
    Timer* tm2 = NULL;
    TimerInit(&tm2, 2);
    CHECK(TimerAlloc(tm2, 1, NULL) == 1 && TimerAlloc(tm2, 2, NULL) == 1, "小容量定时器登记 2 个连接");
    CHECK(TimerAlloc(tm2, 3, NULL) == 1, "第 3 个连接触发自动扩容");
    CHECK(tm2->q->elem_end == 4, "扩容后 3 个条目完好");
    CHECK(TimerFree(tm2, 2) == 1 && TimerFree(tm2, 1) == 1 && TimerFree(tm2, 3) == 1,
          "扩容后可正常移除全部连接");

    //------------ 3.超时关闭（TimerOvertime → FdClose 408） ------------
    Timer* tm3 = NULL;
    TimerInit(&tm3, 8);
    PriorQueueTimerInsert(tm3->q, 42, time(NULL) - 3); //3 秒前的连接，早该超时
    g_closed_fd = -1;
    g_closed_reason = 0;
    CHECK(TimerOvertime(tm3, 1, NULL) == 1, "TimerOvertime 扫描到超时连接");
    CHECK(g_closed_fd == 42 && g_closed_reason == 408, "超时连接以 408 关闭");
    CHECK(tm3->q->queue[0].fd == 0, "超时条目已从堆中弹出");

    Timer* tm4 = NULL;
    TimerInit(&tm4, 8);
    TimerAlloc(tm4, 55, NULL); //刚刚活跃
    g_closed_fd = -1;
    CHECK(TimerOvertime(tm4, 30, NULL) == 1 && g_closed_fd == -1, "未超时（30s 阈值）不关闭连接");
    CHECK(tm4->q->queue[0].fd == 1, "未超时条目仍保留在堆中");

    printf("---- test_timer: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
