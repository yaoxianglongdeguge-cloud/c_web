#include <stdio.h>
#include <stdlib.h>
#include "my_thread/worker_thread.h"
#include "http_analysis/http_main.h"
#include "memory_pool/memory_pool.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>



int main() {
    worker *w = (worker*)malloc(sizeof(worker));
    w->id = 0;
    w->epfd = 0;

    Memory_pool_init(&w->http_pool, 16, 4);

    w->http_ed_store_arr = (http_ed_store**)malloc(sizeof(http_ed_store*));
    Http_ed_store_init(&w->http_ed_store_arr[0], 4096);

    w->my_timer = NULL;
    w->send_pool = NULL;
    w->send_thing_queue = NULL;
    w->send_tool = NULL;
    w->send_tool_table = NULL;
    w->store_pool_table = NULL;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 1);

    int conn = accept(fd, NULL, NULL);
    int flags = fcntl(conn, F_GETFL, 0);   // 从内核拿到当前标志
    fcntl(conn, F_SETFL, flags | O_NONBLOCK); // 加上非阻塞，写回内核

    int a = Http_main(conn, w);

    close(conn);
    close(fd);
    return 0;
}