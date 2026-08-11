#include "http_ed_store.h"
#include "http_state.h"



http_ed_store* Http_ed_store_init(http_ed_store* h,int size)//分配整个存储大小
{
    h=NULL;
    h=(http_ed_store*)malloc(sizeof(http_ed_store));
    
    h->begin=malloc(size);
    h->end=h->begin+size;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;

    http_state* ht;
    int a=http_state_init(ht);
    if(a!=1)
    {
        return NULL;
    }

    h->httpstate=ht;

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
    if(end>=h->end)
    {
        return 0;
    }
    int copy_num=end-h->begin+1;
    memcpy(target, h->begin, copy_num);

    h->ptr_b=h->begin+copy_num;

    int copy_num2=h->ptr_e-h->ptr_b;

    if(h->ptr_b[0]=="\r"&&h->ptr_b[1]=="\n"&&h->ptr_b[2]=="\r"&&h->ptr_b[3]=="\n")
    {
        h->ptr_b=h->ptr_b+4;//防止复制后参与分隔符
    }

    memcpy(h->begin,h->ptr_b,copy_num2);
    h->ptr_b=h->begin;
    h->ptr_e=h->ptr_b+copy_num2;

    return 1;
}







