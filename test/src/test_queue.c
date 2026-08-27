/* test_queue.c
 * 测试三个队列: Task_queue / Send_thing_queue / Memory_Queue
 * 单线程功能 + Task_queue 双线程生产者-消费者
 */
#include "../../include.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

static void test_task_queue(void)
{
    Task_queue* q = NULL;
    Task_queue_init(&q, 4);
    CHECK(q != NULL, "Task_queue init(容量4)");

    /* 容量内 push/pop */
    int e = 0;
    for (int i = 0; i < 4; i++) {
        int r = Task_queue_push(q, NULL, 100 + i, i, 200, (char*)(long)i, 1);
        if (r != 1) { CHECK(0, "Task_queue push"); return; }
    }
    CHECK(q->num == 4, "push 4 个后 num==4");

    Task_Entry t;
    for (int i = 0; i < 4; i++) {
        t = Task_queue_top_and_pop(q, &e);
        if (e != 1 || t.serial != i) { CHECK(0, "Task_queue pop 顺序"); return; }
    }
    CHECK(e == 1 && t.serial == 3, "pop 顺序 FIFO 正确");

    /* 满队列再 push 会阻塞, 用非阻塞方式验证: 起一个线程 pop */
    pthread_t th;
    int popped = 0;
    void* pop_worker(void* arg) {
        Task_queue* qq = (Task_queue*)arg;
        int e2 = 0;
        Task_Entry tt = Task_queue_top_and_pop(qq, &e2);
        if (e2 == 1 && tt.fd == 777) popped = 1;
        return NULL;
    }
    pthread_create(&th, NULL, pop_worker, q);
    usleep(100 * 1000);   /* 让 pop 线程阻塞在 notempty */
    Task_queue_push(q, NULL, 777, 99, 200, NULL, 0);
    pthread_join(th, NULL);
    CHECK(popped == 1, "双线程 push/pop 同步正确(信号量唤醒)");

    /* 双生产者双消费者压力测试 */
    #define N 1000
    static int sum = 0;
    void* producer(void* arg) {
        Task_queue* qq = (Task_queue*)arg;
        for (int i = 0; i < N; i++) Task_queue_push(qq, NULL, i, i, 200, NULL, 0);
        return NULL;
    }
    void* consumer(void* arg) {
        Task_queue* qq = (Task_queue*)arg;
        for (int i = 0; i < N; i++) {
            int e2 = 0;
            Task_Entry tt = Task_queue_top_and_pop(qq, &e2);
            if (e2 == 1) __sync_fetch_and_add(&sum, tt.fd);
        }
        return NULL;
    }
    pthread_t p[2], c[2];
    pthread_create(&p[0], NULL, producer, q);
    pthread_create(&p[1], NULL, producer, q);
    pthread_create(&c[0], NULL, consumer, q);
    pthread_create(&c[1], NULL, consumer, q);
    pthread_join(p[0], NULL); pthread_join(p[1], NULL);
    pthread_join(c[0], NULL); pthread_join(c[1], NULL);
    /* 期望 sum = 2*sum(0..N-1) = N*(N-1) */
    CHECK(sum == N * (N - 1), "双生产者双消费者共 2*N 个任务全部正确消费");
}

static void test_send_thing_queue(void)
{
    Send_thing_queue* q = NULL;
    Send_thing_queue_init(&q, 3);
    int e = 0;
    for (int i = 0; i < 3; i++)
        Send_thing_queue_push(q, NULL, 10 + i, i, 200, 10, 0, (char*)(long)i, -1, 0);
    Send_tq_Entry s;
    for (int i = 0; i < 3; i++) {
        s = Send_thing_queue_top_and_pop(q, &e);
        if (e != 1 || s.serial != i) { CHECK(0, "Send_thing pop"); return; }
    }
    CHECK(e == 1 && s.serial == 2, "Send_thing_queue FIFO 正确");
    /* 空队列 pop */
    s = Send_thing_queue_top_and_pop(q, &e);
    CHECK(e == 0 && s.fd == -1, "空队列 pop 返回 error=0");
}

static void test_memory_queue(void)
{
    Memory_Queue* q = NULL;
    Memory_Queue_init(&q, 3);
    int e = 0;
    for (int i = 0; i < 3; i++)
        Memory_Queue_push(q, 100 + i, (char*)(long)i);
    Memory_Queue_Entry m;
    for (int i = 0; i < 3; i++) {
        m = Memory_Queue_top_and_pop(q, &e);
        if (e != 1 || m.size != 100 + i) { CHECK(0, "Memory_Queue pop"); return; }
    }
    CHECK(e == 1 && m.size == 102, "Memory_Queue FIFO 正确");
    m = Memory_Queue_top_and_pop(q, &e);
    CHECK(e == 0 && m.char_ptr == NULL, "空队列 pop 返回 error=0");
}

int main(void)
{
    printf("===== test_queue =====\n");
    test_task_queue();
    test_send_thing_queue();
    test_memory_queue();
    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}
