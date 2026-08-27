#include "../include.h"


int Http_ed_store_init(http_ed_store** h)//分配整个存储大小，单位是字节
{
    (*h)=(http_ed_store*)malloc(sizeof(http_ed_store));
  ;
    (*h)->begin=NULL;
    (*h)->end=(*h)->begin;
    (*h)->ptr_b=(*h)->begin;
    (*h)->ptr_e=(*h)->begin;

    http_state* ht=NULL;
    int a=http_state_init(&ht);
    if(a!=1)
    {
        return -1;
    }

    (*h)->httpstate=ht;

    return 1;

}

int Http_ed_store_destroy(http_ed_store* h,Memory_Pool* pool)
{
    if(h->begin!=NULL)
    {
        Http_ed_store_free(h,pool);
    }

    free(h);
    return 1;
}

int Http_ed_store_alloc(http_ed_store* h,Memory_Pool* pool,int size)//单位是kb
{
     void* ptr=NULL;
     int notfull=0;
    Memory_Pool_alloc(pool,size,&ptr,&notfull);
    if(notfull==0)
    {
        return -1;
    }

    h->begin=(char*)ptr;
    h->end=h->begin+(size*1024)-1;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;

    return 1;
}

int Http_ed_store_free(http_ed_store* h,Memory_Pool* pool)
{
    int size=h->end-h->begin+1;
    Memory_Pool_free(pool,h->begin,size);
    h->begin=NULL;
    h->end=h->begin;
    h->ptr_b=h->begin;
    h->ptr_e=h->begin;

    http_state_reset(h);

    return 1;
}

int Http_ed_store_expend(http_ed_store* h,Memory_Pool* pool)
{
    int nowsize=h->end-h->begin;
    int expendsize=nowsize*2/1024+1;
    int ptr_b=h->ptr_b-h->begin;
    int ptr_e=h->ptr_e-h->begin;
    void* ptr=NULL;

    int notfull=0;
    Memory_Pool_alloc(pool,expendsize,&ptr,&notfull);
    if(notfull==0)
    {
        return -1;
    }
    memcpy(ptr,h->begin,nowsize);
    Http_ed_store_free(h,pool);
    h->begin=(char*)ptr;
    h->end=h->begin+expendsize*1024-1;
    h->ptr_b=h->begin+ptr_b;
    h->ptr_e=h->begin+ptr_e;

    return 1;

}

int Http_ed_store_write(http_ed_store* h,int fd)
{
   
    int length=h->end-h->ptr_e;
    int n1=read(fd,h->ptr_e,length);

    if(n1>0)
    {
        h->ptr_e=h->ptr_e+n1;

    }
    else if(n1==0)
    {
        return 0;
    }
    else if(n1==-1&&errno==EAGAIN)
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
    target[copy_num]='\0';

    h->ptr_b=h->begin+copy_num;//指针指向剩下的，也就是

    
    if(h->ptr_b[0]=='\r'&&h->ptr_b[1]=='\n'&&h->ptr_b[2]=='\r'&&h->ptr_b[3]=='\n')
    {
        h->ptr_b=h->ptr_b+4;//防止复制后参与分隔符
    }
    
    int copy_num2=h->ptr_e-h->ptr_b;
    if(copy_num2!=0)
    {
        memcpy(h->begin,h->ptr_b,copy_num2);
    }
    
    h->ptr_b=h->begin;
    h->ptr_e=h->ptr_b+copy_num2;
    *(h->ptr_e)='\0';

    return 1;
}







