#include "../../Include/Variate.h"

//typedef struct Request_Temp_Pool Request_Temp_Pool;
//这个就是http解析前存储的地方，需要解决的问题是，如果我们一次读固定长度数据，那就有可能除了读到所要的请求之外还读到下一个请求的一部分，而如果没有标记，
//那我们下次继续读可能读到的是上上次请求残留的无效位，而如果直接再接收从头读，那就有可能这次请求的上一半段还没读到
typedef struct Request_Temp_Pool{

    char* begin;//指向整体最开始
    char* ptr_b;//指向顺序来看，有效位置的最开始
    char* ptr_e;//指向顺序来看，有效位置末端后一位.
    char* end;//指向整体末端后一位
    State_Machine* state_machine;//http状态机，放在这里刚好用暂存区的指针
    int from_malloc;//1=当前暂存块来自 malloc（大请求扩容时绕过内存池，避免"旧块+新块"撑爆子池）

}Request_Temp_Pool;//这个就是http解析前存储的地方，需要解决的问题是，如果我们一次读固定长度数据，那就有可能除了读到所要的请求之外还读到下一个请求的一部分，而如果没有标记，
//那我们下次继续读可能读到的是上上次请求残留的无效位，而如果直接再接收从头读，那就有可能这次请求的上一半段还没读到



int RequestTempPoolInit(Request_Temp_Pool** h);//分配整个存储大小

int RequestTempPoolDestory(Request_Temp_Pool* h,Memory_Pool* pool);

int RequestTempPoolAlloc(Request_Temp_Pool* h,Memory_Pool* pool,int size);//单位是kb

int RequestTempPoolFree(Request_Temp_Pool* h,Memory_Pool* pool);

int RequestTempPoolExpend(Request_Temp_Pool* h,Memory_Pool* pool,int* hadBigest);

//按指定大小扩容（needSize>0 时精确扩到 needSize，<=0 时按 2 倍扩）；不会丢已读数据
int RequestTempPoolExpendTo(Request_Temp_Pool* h,Memory_Pool* pool,int needSize,int* hadBigest);

int RequestTempPoolWrite(Request_Temp_Pool* h,int fd);//从连接中读数据填满无效位

int RequestTempPoolCopy(Request_Temp_Pool* h,char*end,char* target);//从有效位开始到要拷贝数据末端(包含末端)把数据拷贝到指定位置(包含该位置)，如果要拷贝的范围大于有效范围则返回2
//end是要拷贝的末端，target是拷贝后存储位置

int RequestTempPoolStateReset(Request_Temp_Pool* hs);