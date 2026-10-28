#include "../Include/Include.h"

//任务队列不再全局唯一：每个接待线程在 ReceiverThreadInit 里建自己的队列，
//这里只保存容量，供接待线程初始化时使用

Router* router;

int cweb_listen_port=0;

int GlobalResourceInit(RouterFunc Handler)
{

    router=(Router*)malloc(sizeof(Router));
    RouterInit();
    Handler();

    return 1;
}





