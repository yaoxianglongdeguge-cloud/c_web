#include "Include/Variate.h"
#include "Include/Include.h"
#include "WebApp/WebApp.h"

int main(){

    // WebStater 流程：忽略 SIGPIPE -> 初始化全局路由表/任务队列 -> 回调
    // WebAppRegisterRoutes() 注册路由 -> 创建监听 socket -> 启动线程；
    // 当前线程随后进入 Receiver 事件循环（阻塞）。
    WebStater("127.0.0.1",5343,WebAppRegisterRoutes);
    return 0;
}