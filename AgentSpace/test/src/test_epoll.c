#include "../../Include/Include.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [PASS] %s\n", msg); }                       \
        else { g_fail++; printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

int main(void)
{
    printf("== test_epoll : epoll 关注位（读/写事件挂载与重挂载）==\n");

    printf("  [info] 表达式 EPOLLIN || EPOLLOUT = %d，而 EPOLLIN | EPOLLOUT = %d\n",
           EPOLLIN || EPOLLOUT, EPOLLIN | EPOLLOUT);

    int epfd = epoll_create(64);
    CHECK(epfd > 0, "epoll_create 成功");

    int p[2];
    CHECK(pipe(p) == 0, "建立管道模拟连接（p[0] 读端 / p[1] 写端）");

    struct epoll_event evs[8];
    int n = 0;

    //-------- 1.对照：只挂可读，写端空闲时不应上报可写 --------
    EpollReadMount(epfd, p[1]);
    n = epoll_wait(epfd, evs, 8, 200);
    CHECK(n == 0, "对照：只挂 EPOLLIN 时写端空闲不上报事件（超时 200ms）");

    //-------- 2.同时关注读+写：框架发完一段响应后靠它重新挂载 --------
    EpollChangeReAndWr(epfd, p[1]);
    n = epoll_wait(epfd, evs, 8, 300);
    printf("  [info] EpollChangeReAndWr 后 epoll_wait 返回 %d 个事件\n", n);
    CHECK(n == 1 && (evs[0].events & EPOLLOUT) != 0,
          "EpollChangeReAndWr 后必须收到 EPOLLOUT（写端可写）");

    //-------- 3.发送缓冲写满 → 恢复可写后必须能被唤醒（大文件下载依赖此点）--------
    int flags = fcntl(p[1], F_GETFL, 0);
    fcntl(p[1], F_SETFL, flags | O_NONBLOCK);
    char buf[4096];
    memset(buf, 'x', sizeof(buf));
    long total = 0;
    ssize_t w;
    while ((w = write(p[1], buf, sizeof(buf))) > 0) total += w;
    CHECK(total > 0, "把管道写满（模拟 socket 发送缓冲被写满）");

    n = epoll_wait(epfd, evs, 8, 300);
    CHECK(n == 0, "缓冲已满时不上报 EPOLLOUT（正常语义）");

    char drain[8192];
    ssize_t rd = read(p[0], drain, sizeof(drain));
    CHECK(rd > 0, "从对端读走部分数据（腾出缓冲空间）");

    n = epoll_wait(epfd, evs, 8, 300);
    printf("  [info] 缓冲腾空后 epoll_wait 返回 %d 个事件\n", n);
    CHECK(n == 1 && (evs[0].events & EPOLLOUT) != 0,
          "缓冲腾出空间后必须收到 EPOLLOUT 以继续发送剩余数据");

    //-------- 4.回到只读关注 + 摘除 --------
    EpollChangeRead(epfd, p[1]);
    n = epoll_wait(epfd, evs, 8, 150);
    CHECK(n == 0, "EpollChangeRead 后不再关注可写（回到只读）");

    EpollDelete(epfd, p[1]);
    EpollDelete(epfd, p[0]);
    n = epoll_wait(epfd, evs, 8, 150);
    CHECK(n == 0, "EpollDelete 后该 fd 不再产生事件");

    close(p[0]);
    close(p[1]);
    close(epfd);

    printf("---- test_epoll: PASS %d / FAIL %d ----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
