#include <stdio.h>
#include <stdlib.h>
#include "my_thread/worker_thread.h"
#include "my_thread/profession_thread.h"
#include "http_analysis/http_main.h"
#include "memory_pool/memory_pool.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "server/global_resource.h"
#include <time.h>
#include <arpa/inet.h>
#include <netinet/tcp.h> 
#include <string.h>       // strlen, memset, strstr 等

typedef  struct main{
    worker* w;
    int fd;
    int time;

} main_t;

void* func1(void* arg)
{
    main_t* m=(main_t*)arg;
    receive_and_send_main(m->w,m->fd,m->time);
}
void* func2(void* arg)
{
    while(1)
    {
    deal_and_pack();
    }
}

int main() {

   global_resource_init(100);

int fd = socket(AF_INET, SOCK_STREAM, 0);
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;
addr.sin_port = htons(8080);

bind(fd, (struct sockaddr*)&addr, sizeof(addr));
listen(fd, 100);
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

// ========== 在这里插入预热代码 ==========
printf("开始预热...\n");

// 1. 预热TCP连接（自连接）
int warmup_client = socket(AF_INET, SOCK_STREAM, 0);
if (warmup_client >= 0) {
    struct sockaddr_in warmup_addr;
    warmup_addr.sin_family = AF_INET;
    warmup_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    warmup_addr.sin_port = htons(8080);
    
    if (connect(warmup_client, (struct sockaddr*)&warmup_addr, sizeof(warmup_addr)) == 0) {
        printf("预热连接成功\n");
        
        // 接受这个预热连接
        int warmup_server = accept(fd, NULL, NULL);
        if (warmup_server >= 0) {
            // 设置非阻塞
            int warmup_flags = fcntl(warmup_server, F_GETFL, 0);
            fcntl(warmup_server, F_SETFL, warmup_flags | O_NONBLOCK);
            
            // 设置TCP_NODELAY
            int warmup_opt = 1;
            setsockopt(warmup_server, IPPROTO_TCP, TCP_NODELAY, &warmup_opt, sizeof(warmup_opt));
            
            // 发送测试请求
            const char *test_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
            write(warmup_client, test_request, strlen(test_request));
            
            // 读取请求
            char warmup_buf[4096];
            int warmup_len = read(warmup_server, warmup_buf, sizeof(warmup_buf));
            if (warmup_len > 0) {
                printf("预热读取到 %d 字节\n", warmup_len);
                
                // 发送响应
                const char *test_response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "Hello";
                write(warmup_server, test_response, strlen(test_response));
            }
            
            close(warmup_server);
        }
        close(warmup_client);
    } else {
        perror("预热连接失败");
        close(warmup_client);
    }
}

// 2. 预热多次连接（可选，更彻底）
for (int i = 0; i < 3; i++) {
    int pre_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (pre_fd >= 0) {
        struct sockaddr_in pre_addr;
        pre_addr.sin_family = AF_INET;
        pre_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        pre_addr.sin_port = htons(8080);
        
        if (connect(pre_fd, (struct sockaddr*)&pre_addr, sizeof(pre_addr)) == 0) {
            int pre_server = accept(fd, NULL, NULL);
            if (pre_server >= 0) {
                // 快速测试
                char pre_buf[1024];
                write(pre_fd, "GET / HTTP/1.1\r\n\r\n", 18);
                read(pre_server, pre_buf, sizeof(pre_buf));
                write(pre_server, "HTTP/1.1 200 OK\r\n\r\nOK", 22);
                close(pre_server);
            }
        }
        close(pre_fd);
    }
}

printf("预热完成\n");
// ========== 预热代码结束 ==========

// 创建worker
worker* w1;
worker_init(&w1, fd, 1);
worker* w2;
worker_init(&w2, fd, 2);
worker* w3;
worker_init(&w3, fd, 3);
worker* w4;
worker_init(&w4, fd, 4);

pthread_t tid1;
pthread_t tid2;

main_t m;
m.fd = fd;
m.time = 10000;
m.w = w1;

pthread_create(&tid2, NULL, func2, NULL);
pthread_create(&tid2, NULL, func2, NULL);

pthread_create(&tid2, NULL, func2, NULL);

pthread_create(&tid2, NULL, func2, NULL);


pthread_create(&tid1, NULL, func1, &m);
pthread_create(&tid1, NULL, func1, &m);
pthread_create(&tid1, NULL, func1, &m);
pthread_create(&tid1, NULL, func1, &m);

while(1)
{
    sleep(100);
}

return 0;
}