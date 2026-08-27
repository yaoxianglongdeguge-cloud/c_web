/* test_rwlock.c
 * 测试 my_rwlock_t：验证写锁独占性、读写锁状态机、读者阻塞行为
 * 修复后 rdlock 会正确阻塞在写锁上, 因此用线程+超时验证
 */
#include "../../include.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("[ OK ] %s\n", msg); } \
} while (0)

static my_rwlock_t rw;
static int reader_entered = 0;
static int writer_entered = 0;

static void* reader_thread(void* arg)
{
    (void)arg;
    my_lock_rdlock(&rw);
    reader_entered = 1;
    my_lock_unlock(&rw);
    return NULL;
}
static void* writer_thread(void* arg)
{
    (void)arg;
    my_lock_wrlock(&rw);
    writer_entered = 1;
    my_lock_unlock(&rw);
    return NULL;
}

int main(void)
{
    printf("===== test_rwlock =====\n");
    my_rwlock_init(&rw);

    /* 场景1: 写锁独占 —— 写锁持有时, 读锁应被阻塞 */
    my_lock_wrlock(&rw);
    CHECK(rw.writers == 1, "wrlock 后 writers==1");
    CHECK(rw.readers == 0, "wrlock 后 readers==0");

    pthread_t th;
    pthread_create(&th, NULL, reader_thread, NULL);
    usleep(200 * 1000);                 /* 给读者线程时间尝试进入 */
    CHECK(reader_entered == 0, "写锁持有时读锁被阻塞(reader_entered==0)");
    my_lock_unlock(&rw);                /* 释放写锁 */
    pthread_join(th, NULL);
    CHECK(reader_entered == 1, "释放写锁后读锁可进入");

    /* 场景2: 写锁互斥 —— 写锁持有时, 另一个写者应被阻塞 */
    my_lock_wrlock(&rw);
    pthread_t th2;
    pthread_create(&th2, NULL, writer_thread, NULL);
    usleep(200 * 1000);
    CHECK(writer_entered == 0, "写锁持有时另一写者被阻塞");
    my_lock_unlock(&rw);
    pthread_join(th2, NULL);
    CHECK(writer_entered == 1, "释放写锁后写者可进入");

    /* 场景3: 读锁可共享 */
    my_lock_rdlock(&rw);
    my_lock_rdlock(&rw);
    CHECK(rw.readers == 2, "两个读锁可共存(期望 readers==2)");
    my_lock_unlock(&rw);
    my_lock_unlock(&rw);
    CHECK(rw.readers == 0 && rw.writers == 0 && rw.waiting_writers == 0,
          "全部释放后锁状态清零");

    printf("\n结果: %s (%d 个失败)\n",
           failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}
