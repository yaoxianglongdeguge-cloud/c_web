#include<stdio.h>

typedef struct http_state http_state;

typedef struct http_ed_store{

    char* begin;//指向整体最开始
    char* ptr_b;//指向顺序来看，有效位置的最开始
    char* ptr_e;//指向顺序来看，有效位置末端后一位.
    char* end;//指向整体末端后一位
    http_state* httpstate;//http状态机，放在这里刚好用暂存区的指针

}http_ed_store;//这个就是http解析前存储的地方，需要解决的问题是，如果我们一次读固定长度数据，那就有可能除了读到所要的请求之外还读到下一个请求的一部分，而如果没有标记，
//那我们下次继续读可能读到的是上上次请求残留的无效位，而如果直接再接收从头读，那就有可能这次请求的上一半段还没读到



int Http_ed_store_init(http_ed_store** h,int size);//分配整个存储大小

int Http_ed_store_write(http_ed_store* h,int fd);//从连接中读数据填满无效位

int Http_ed_store_copy(http_ed_store* h,char*end,char* target);//从有效位开始到要拷贝数据末端(包含末端)把数据拷贝到指定位置(包含该位置)，如果要拷贝的范围大于有效范围则返回2
//end是要拷贝的末端，target是拷贝后存储位置