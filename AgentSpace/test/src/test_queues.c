#include "../../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

int main(void)
{
    printf("== test_queues : WorkerQueue / ReceiverQueue / TaskQueue ==\n");

    /* ================= 1. WorkerQueue（无上限链表，节点搭在块尾部） ================= */
    Worker_Queue* wq = NULL;
    CHECK(WorkerQueueInit(&wq) == 1, "WorkerQueueInit 成功");
    CHECK(wq->hadNum == 0, "初始为空");

    char* wb[3];
    int   wsz[3] = { 100, 200, 300 };
    for (int i = 0; i < 3; i++) wb[i] = (char*)malloc(WORKER_QUEUE_ALLOC_BYTES(wsz[i]));

    CHECK(WorkerQueuePush(wq, wsz[0], wb[0]) == 1, "push 块1");
    CHECK(WorkerQueuePush(wq, wsz[1], wb[1]) == 1, "push 块2");
    CHECK(WorkerQueuePush(wq, wsz[2], wb[2]) == 1, "push 块3（没有容量上限）");
    CHECK(wq->hadNum == 3, "hadNum == 3");

    int e = 0;
    Worker_Queue_Entry* list = WorkerQueueTopAndPop(wq, &e);
    CHECK(e == 1 && list != NULL, "一次摘下整条链");

    int n = 0, order = 1;
    for (Worker_Queue_Entry* m = list; m != NULL; m = m->next) {
        if (n >= 3 || m->NeedFree != wb[n]) order = 0;
        n++;
    }
    CHECK(n == 3 && order == 1, "FIFO 顺序正确，NeedFree 指向块基址");
    CHECK(wq->hadNum == 0, "摘链后 hadNum == 0");

    e = 1;
    CHECK(WorkerQueueTopAndPop(wq, &e) == NULL && e == 0, "空队列返回 NULL 且 error=0");

    CHECK(WorkerQueuePush(wq, wsz[0], wb[0]) == 1, "排空后可继续 push");
    e = 0;
    list = WorkerQueueTopAndPop(wq, &e);
    CHECK(e == 1 && list != NULL && list->NeedFree == wb[0], "再次摘下同一块");

    for (int i = 0; i < 3; i++) free(wb[i]);

    /* ================= 2. ReceiverQueue（响应事件队列 + 临时链） ================= */
    Receiver_Queue* rq = NULL;
    CHECK(ReceiverQueueInit(&rq) == 1, "ReceiverQueueInit 成功");
    CHECK(ReceiverQueueLen(rq) == 0, "初始长度 0");

    int payload = 64;
    void* rb[2];
    for (int i = 0; i < 2; i++) rb[i] = malloc(RECEIVER_QUEUE_ALLOC_BYTES(payload));

    CHECK(ReceiverQueuePush(rq, NULL, 7, 0x1001ULL, 1, 200, 4096, 0, 0, (char*)0xA1, -1, 0, rb[0], payload) == 1,
          "push 响应事件1");
    CHECK(ReceiverQueuePush(rq, NULL, 8, 0x1002ULL, 2, 404, 128, 0, 0, (char*)0xA2, -1, 0, rb[1], payload) == 1,
          "push 响应事件2");
    CHECK(ReceiverQueueLen(rq) == 2, "长度 2");

    Receiver_Queue_Entry* rlist = ReceiverQueueTopAndPop(rq, &e);
    CHECK(e == 1 && rlist != NULL, "一次摘下整条响应链");
    CHECK(rlist->fd == 7 && rlist->serial == 1 && rlist->onlyId == 0x1001ULL
          && rlist->error_reason == 200 && rlist->size_resp == 4096 && rlist->char_ptr == (char*)0xA1,
          "事件1 字段正确（含 onlyId）");
    CHECK(rlist->next != NULL && rlist->next->fd == 8 && rlist->next->onlyId == 0x1002ULL,
          "事件2 紧随其后，字段正确");
    CHECK(ReceiverQueueLen(rq) == 0, "摘链后长度 0");
    CHECK(ReceiverQueueTopAndPop(rq, &e) == NULL && e == 0, "空队列返回 NULL");

    /* 临时链：验证 ReceiverQueueEntryCopy 把 onlyId 也带过去 */
    Receiver_Queue_Temp* rt = NULL;
    CHECK(ReceiverQueueTempInit(&rt) == 1, "ReceiverQueueTempInit 成功");
    CHECK(ReceiverQueueTempPush(rt, rlist) == 1, "把响应链挂到临时链");
    Receiver_Queue_Entry s = ReceiverQueueTempTopAndPop(rt, &e);
    CHECK(e == 1 && s.fd == 7 && s.onlyId == 0x1001ULL && s.char_ptr == (char*)0xA1,
          "临时链出队1：字段完整（onlyId 不丢）");
    s = ReceiverQueueTempTopAndPop(rt, &e);
    CHECK(e == 1 && s.fd == 8 && s.onlyId == 0x1002ULL, "临时链出队2：顺序与字段正确");
    s = ReceiverQueueTempTopAndPop(rt, &e);
    CHECK(e == 0, "临时链空后 error=0");

    for (int i = 0; i < 2; i++) free(rb[i]);

    /* ================= 3. TaskQueue（容量闸门 + 原子认领） ================= */
    Task_Queue* tq = NULL;
    CHECK(TasKQueueInit(&tq, 3) == 1, "TasKQueueInit(3) 成功");
    CHECK(tq->num == 0, "初始为空");

    CHECK(TasKQueuePush(tq, NULL, 100, 0x2001ULL, 1, 200, (char*)0xB1, 64) == 1, "push 任务1");
    CHECK(TasKQueuePush(tq, NULL, 101, 0x2002ULL, 2, 200, (char*)0xB2, 128) == 1, "push 任务2");
    CHECK(TasKQueuePush(tq, NULL, 102, 0x2003ULL, 3, 200, (char*)0xB3, 256) == 1, "push 任务3（刚满）");
    CHECK(tq->num == 3, "num == 3");

    CHECK(TasKQueuePush(tq, NULL, 103, 0x2004ULL, 4, 200, NULL, 0) == 0, "第 4 个被拒（容量闸门）");
    CHECK(tq->num == 3, "被拒后计数不变");

    Task_Queue_Entry t = TasKQueueTopAndPop(tq, &e);
    CHECK(e == 1 && t.fd == 100 && t.serial == 1 && t.onlyId == 0x2001ULL && t.h_size == 64,
          "任务1 字段正确（含 onlyId）");
    t = TasKQueueTopAndPop(tq, &e);
    CHECK(e == 1 && t.fd == 101 && t.onlyId == 0x2002ULL, "任务2 字段正确");
    t = TasKQueueTopAndPop(tq, &e);
    CHECK(e == 1 && t.fd == 102 && t.onlyId == 0x2003ULL, "任务3 字段正确");
    CHECK(tq->num == 0, "取空后 num == 0");

    t = TasKQueueTopAndPop(tq, &e);
    CHECK(e == 0 && t.fd == -1, "空队列返回默认条目且 error=0");

    CHECK(TasKQueuePush(tq, NULL, 200, 0x2005ULL, 5, 200, NULL, 0) == 1, "排空后可继续 push");

    printf("---- test_queues: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
