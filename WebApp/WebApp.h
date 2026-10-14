#include "../Include/Variate.h"

// 网站应用层：把 URL 与处理函数注册到框架路由表。
//
// 用法（见 main.c）：
//     WebStater("127.0.0.1", 5343, WebAppRegisterRoutes);
// WebStater 内部会先 RouterInit() 建表，再回调本函数完成注册，最后才启动线程，
// 所以这里注册路由是线程安全的（此时没有任何连接/业务线程在跑）。
void WebAppRegisterRoutes(void);
