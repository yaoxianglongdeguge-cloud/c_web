#include<stdio.h>


http_ed_store* Http_ed_store_init(http_ed_store* h,int size);//分配整个存储大小

int Http_ed_store_accept(http_ed_store* h,int fd);//从连接中读数据填满无效位

int Http_ed_store_copy(http_ed_store* h,char*end,char* target);//从有效位开始到要拷贝数据末端(包含末端)把数据拷贝到指定位置(包含该位置)，如果要拷贝的范围大于有效范围则返回2
//end是要拷贝的末端，target是拷贝后存储位置