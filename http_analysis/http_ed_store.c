#include "http_ed_store.h"

typedef struct http_ed_store{

    char* begin;//指向整体最开始
    char* ptr_b;//指向顺序来看，有效位置的最开始
    char* ptr_e;//指向顺序来看，有效位置末端后一位.
    char* end;//指向整体末端后一位

}http_ed_store;//这个就是http解析前存储的地方，需要解决的问题是，如果我们一次读固定长度数据，那就有可能除了读到所要的请求之外还读到下一个请求的一部分，而如果没有标记，
//那我们下次继续读可能读到的是上上次请求残留的无效位，而如果直接再接收从头读，那就有可能这次请求的上一半段还没读到


http_ed_store* Http_ed_store_init(http_ed_store* h,int size)//分配整个存储大小
{
    h=NULL;
    h=(http_ed_store*)malloc(sizeof(http_ed_store));
    
    h->begin=malloc(size);
    h->end=h->begin+size;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;

    return h;

}

int Http_ed_store_accept(http_ed_store* h,int fd)
{
   
        int length=h->end-h->ptr_e;
        int n1=read(fd,h->ptr_e,length);
        if(n1==1)
        {
         h->ptr_e=h->end;
        }
        else
        {
            return -1;
        }

    return 1;
}

int Http_ed_store_copy(http_ed_store* h,char*end,char* target)//把一整个请求拷走，然后把后面的数据拷到开头
{
    int copy_num=end-h->begin+1;
    memcpy(target, h->begin, copy_num);

    h->ptr_b=h->begin+copy_num;

    int copy_num2=h->ptr_e-h->ptr_b;
    memcpy(h->begin,h->ptr_b,copy_num2);
    h->ptr_b=h->begin;
    h->ptr_e=h->ptr_b+copy_num2;

    return 1;
}







