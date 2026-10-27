#include "Include/Include.h"
#include <stdlib.h>

// QPS 测试入口：不依赖 WebApp/静态目录，只提供三个路由
//   /fast   立即返回
//   /slow   BENCH_SLOW_US（默认 10ms）后返回
//   /mix    每 10 个请求有 1 个走 slow
// 端口由 BENCH_PORT 环境变量指定，默认 15443

static _Atomic int g_mix;

static int slow_us(void){
    const char* s=getenv("BENCH_SLOW_US");
    int us=(s!=NULL)?atoi(s):10000;
    return (us>0)?us:10000;
}

static void h_fast(Request* q,Response* w){
    (void)q;
    ResponseBodySet(w,(char*)"ok");
}

static void h_slow(Request* q,Response* w){
    (void)q;
    usleep((useconds_t)slow_us());
    ResponseBodySet(w,(char*)"ok");
}

static void h_mix(Request* q,Response* w){
    (void)q;
    int n=atomic_fetch_add(&g_mix,1);
    if(n%10==0) usleep((useconds_t)slow_us());
    ResponseBodySet(w,(char*)"ok");
}

static void reg(void){
    RouterAppend((char*)"/fast",h_fast);
    RouterAppend((char*)"/slow",h_slow);
    RouterAppend((char*)"/mix",h_mix);
}

int main(void){
    const char* p=getenv("BENCH_PORT");
    int port=(p!=NULL)?atoi(p):15443;
    WebStater((char*)"127.0.0.1",port,reg);
    return 0;
}
