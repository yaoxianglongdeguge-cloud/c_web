#include "hash_2.h"
#include <stdio.h>
#include<stdint.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include"../memory_pool/memory_pool.h"
#include "../http_analysis/http_analysis.h"

int Hash_map;
static const int PRIME_BUCKET_SIZES_2[15];

//type -1是在栈上，0是在堆上，经过内核态，更大的数字就是表明哪种内存池
//上面是错的，在栈上分配的函数结束就释放了，有问题


Hash2_Entry_2* Hash2_Entry_Creat(char* key,void* value,void* ptr);//创建节点,要指明用哪个类型内存池

Hash2_Entry_2* Hash2_Entry_Find(Hash2_Entry_2* head,char* url,int* Error);//查找一个链表内节点，并且返回指向那个节点的指针，如果为NULL则说明节点不存在

int Hash2_Entry_Insert(Hash2_Entry_2* head,char* url,void* func,void* ptr);//插入节点，head为一个链表开头，之后查找，没找到就插入.要指明用哪个类型内存池

int Hash2_Allocate(Hash_map_2* h,int size_h,Http_analysis_1* Http);//分配特定大小内存.要指明用哪个类型内存池

static unsigned long hash_2(const char *str);//哈希函数：DJB2 + 盐值

int bucket_site_2(int bucket_size,const char* url);//桶位置计算


typedef struct Entry{
    
    char* key;//键
    void* value;//值
    struct Entry* next;//指向下一个元素

}Hash2_Entry_2;




Hash2_Entry_2* Hash2_Entry_Creat(char* key,void* value,void* ptr)
{
    Hash2_Entry_2* e;
    e=ptr;
    
    e->key=key;
    e->value=value;
    e->next=NULL;

    return e;
}

Hash2_Entry_2* Hash2_Entry_Find(Hash2_Entry_2* head,char* url,int* Error)
{
    Hash2_Entry_2* h=head;
    *Error=0;
    while(h->next!=NULL)
    {
        if(strcmp(h->next->key,url)==0)
        {
            *Error=1;
            return h;
        }
        h=h->next;
    }

    return h;
}

int Hash2_Entry_Insert(Hash2_Entry_2* head,char* url,void* func,void* ptr)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Hash2_Entry_2* i=Hash2_Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=Hash2_Entry_Creat(url,func,ptr);
        if(i->next!=NULL)
        {

            return 1;
        }
        else
        {
            return -1;
        }
    }

    if(strcmp(i->next->key,url)==0)
    {
        return 0;
    }
    else
    {
        return -1;
    }

    return -1;
}

int Hash2_Allocate(Hash_map_2* h,int size_h,Http_analysis_1* Http)
{

    h->Elem=(Hash2_Entry_2*)(Http->ptr);

    if(h->Elem==NULL)
    {
        return -1;
    }

    Http->ptr=Http->ptr+size_h*(sizeof(Hash2_Entry_2));
    if(Http->ptr>=Http->end)
    {
        return 0;
    }

    for(int i=0;i<size_h;i++)
    {
        
        h->Elem[i].key="woc";
        h->Elem[i].value=NULL;
        h->Elem[i].next=NULL;

    }


    return 1;
}

int Hash2_Init(Http_analysis_1* Http, int init)//1成功，0已存在，-1过程错误,init是哈希表初始大小
{
    Hash_map_2* h;
    h=(Hash_map_2*)Http->ptr;

    int bu_size=PRIME_BUCKET_SIZES_2[init-1];

    Http->ptr=Http->ptr+sizeof(*h);
    if(Http->ptr>=Http->end)
    {
        return 0;
    }

    int a=Hash2_Allocate(h,bu_size,Http);
    if(a==-1)
    {
        return -1;
    }
    else if(a==0)
    {
        return 0;
    }

    h->bu_num=bu_size;
    h->elem_num=0;


    return 1;

}

char* Hash2_Find(Hash_map_2* h,char* url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL)
    {
        *Error=0;
        return NULL;
    }

    int b_site=bucket_site_2(h->bu_num,url);

    Hash2_Entry_2* head=&(h->Elem[b_site]);

    int e=0;

    Hash2_Entry_2* p=Hash2_Entry_Find(head,url,&e);

    if(e==1&&p->next!=NULL&&strcmp(p->next->key,url)==0)
    {
        *Error=1;
        return p->next->value;
    }
    else
    {
        *Error=-1;
    }

    return NULL;

}

int Hash2_Insert(Hash_map_2* h,Http_analysis_1* Http, char* url,void* func)
{

    int b_site=bucket_site_2(h->bu_num,url);

    Hash2_Entry_2* head=&(h->Elem[b_site]);

    Http->ptr=Http->ptr+sizeof(Hash2_Entry_2);
    if(Http->ptr>=Http->end)
    {
        return 0;
    }
    int e=Hash2_Entry_Insert(head,url,func,Http->ptr);

    if(e!=1)
    {
        return -1;
    }


    h->elem_num++;

    return 1;


}
 

static unsigned long hash_2(const char *str) {
    unsigned long h = 5381;
    int c;
    while ((c = *str++))
        h = h * 33 + c;
    return h;
}

int bucket_site_2(int bucket_size,const char* url)
{
    return hash_2(url) % bucket_size;
}

//由于这个表代码是复制的上一个哈希表然后改的，所以有些url，func这种残留不必在意

static const int PRIME_BUCKET_SIZES_2[15] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x

};//用于哈希表扩容

