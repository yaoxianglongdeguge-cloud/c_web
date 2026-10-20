#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>
#include<stdint.h>
#include <time.h>
#include <pthread.h> 
#include <semaphore.h> 
#include <arpa/inet.h>   
#include <unistd.h> 
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdatomic.h>


/* =====================================================================
 *  foo.h —— libcweb（纯 C HTTP 解析 / 收发框架）对外接口头文件
 *
 *  这里只放"上层业务会用到的接口"，与实现无关：
 *    ① 启动：WebStater
 *    ② 路由：RouterAppend / RouterHandlerFind
 *    ③ 请求：RequestGetter（Request 为只读视图）
 *    ④ 响应：ResponseBodySet / ResponseHeaderSet / ResponseFileSet / ResponseErrorSet
 *
 *  用法（自己写一个服务器）：
 *      #include "foo.h"
 *
 *      static void handler_home(Request* r, Response* w)
 *      {
 *          (void)r;
 *          ResponseHeaderSet(w, "Content-Type", "text/plain; charset=utf-8");
 *          ResponseBodySet(w, "hello cweb");
 *      }
 *
 *      static void register_routes(void)          // 路由注册回调
 *      {
 *          RouterAppend("/", handler_home);
 *      }
 *
 *      int main(void)
 *      {
 *          WebStater("127.0.0.1", 8899, register_routes);   // 阻塞在当前线程跑事件循环
 *          return 0;
 *      }
 *
 *  编译（库包根目录为 libcweb/，库产物 libcweb.a）：
 *      gcc app.c -I<libcweb路径> -L<libcweb路径> -lcweb -lpthread -o app
 *
 *  说明：
 *    · 本文件是**对外接口的稳定子集**；内部模块（内存池、队列、fd 表、定时器、
 *      状态机、线程等）的声明见 Include/Include.h，那是框架内部使用的完整清单，
 *      业务代码不需要、也不建议直接调用。
 *    · Request / Response 在此按**不完全类型**声明就够了：业务侧只用
 *      RequestGetter 读请求、用 Response* 系列函数写响应，不直接碰结构体内部字段。
 *    · 本文件与 Include/Include.h 可以同时包含，类型与声明完全一致，不会重复定义报错。
 * ===================================================================== */

#ifndef CWEB_FOO_H
#define CWEB_FOO_H

#include <sys/types.h>   /* off_t（ResponseFileSet 的偏移参数） */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 *  类型
 * ------------------------------------------------------------------- */

/* 请求（只读视图，字段常量；用 RequestGetter 取值，不要直接访问成员） */
typedef struct Request Request;

/* 响应（由框架构造后传给业务处理函数，业务侧只通过下面 4 个函数写入） */
typedef struct Response Response;

/* 业务处理函数类型：注册到路由表后在对应 URL 命中时被调用 */
typedef void (*Handler)(Request* r, Response* w);

/* 启动回调类型：WebStater 建表后、启动线程前调用，用它注册路由 */
typedef void (*RouterFunc)();

/* ---------------------------------------------------------------------
 *  ① 启动：初始化路由表/任务队列容量 -> 回调注册路由 -> 按组建监听 socket -> 起接待/工作线程
 *     各组在独立线程里跑事件循环，当前线程阻塞等待，函数返回即服务结束。
 *     参数：IPaddr 监听地址；PORT 监听端口；Handler 路由注册回调（RouterFunc）
 *     返回：<0 失败（参数/建表/socket 出错），否则不返回。
 * ------------------------------------------------------------------- */
int WebStater(char* IPaddr, int PORT, RouterFunc Handler);

/* ---------------------------------------------------------------------
 *  ② 路由（DJB2+盐值哈希，URL 精确匹配，仅支持启动阶段静态注册）
 *     RouterAppend：把一个 URL 绑到处理函数；成功 1，失败 0（重复注册会失败）。
 *                   url 指针被直接保存不复制 —— 请传静态字符串。
 *                   url 写成不带查询串的路径（如 "/echo"）。
 *     RouterHandlerFind：按 URL 找处理函数；未注册返回 NULL（框架回 404）。
 *     路由表由 WebStater 内部初始化，业务侧不需要调用 RouterInit。
 * ------------------------------------------------------------------- */
int RouterAppend(char* url, Handler func);
Handler RouterHandlerFind(char* url);

/* ---------------------------------------------------------------------
 *  ③ 请求只读取值
 *     get1 ∈ {"Method","Url","Version","Body","Headers","Query"}
 *       · Method / Url / Version / Body：get2 传 NULL，返回对应字符串
 *         （Url 已剥离查询串，如 "/echo"；Body 为请求体，可能为空串）
 *       · Headers / Query：get2 传键名，返回该头/该查询参数的值
 *     未命中返回 NULL（NULL 表示不存在，"空串"和"不存在"是两回事）。
 *     Query 给出的是**原始未解码**串（%XX、'+' 未还原），需要时业务侧自行解码。
 * ------------------------------------------------------------------- */
const char* RequestGetter(Request* r, char* get1, char* get2);

/* ---------------------------------------------------------------------
 *  ④ 响应写入（内存由框架的内存池提供，业务代码不需要 free）
 *
 *     ResponseBodySet  ：设置文本响应体，按 Content-Length 自动补齐头
 *                        成功 1；池满/分配失败自动降级 503
 *     ResponseHeaderSet：追加一个自定义响应头（如 Content-Type）；
 *                        重复设置同名头会被拒绝（返回 0）
 *     ResponseFileSet  ：文件响应（sendfile 零拷贝）：先 open() 得到 fd，
 *                        再调用本函数；发送完成后由**框架负责 close(fd)**，
 *                        Content-Length 由框架 fstat 填充
 *     ResponseErrorSet ：设置错误码（400/404/405/408/411/413/414/431/500/503），
 *                        框架会生成对应的文本响应体
 *     注意：Body 与 File 二选一；只允许响应尚未发出前调用。
 * ------------------------------------------------------------------- */
int ResponseBodySet(Response* r, char* c);
int ResponseHeaderSet(Response* r, char* key, char* value);
int ResponseFileSet(Response* r, int fd, off_t offset);
int ResponseErrorSet(Response* r, int error);

#ifdef __cplusplus
}
#endif

#endif /* CWEB_FOO_H */
