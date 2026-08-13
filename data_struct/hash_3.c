#include "hash_3.h"
#include <stdio.h>
#include<stdint.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


unsigned long salt=0;//随机数防止攻击者发送特定信息都哈希选进一个桶里

static const int PRIME_BUCKET_SIZES[15] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x
 
};//用于哈希表扩容

int PRIME_BUCKET_SIZES_nidex=0;


Hash_Entry_3* Hash3_Entry_Creat(int key,int value);//创建节点

Hash_Entry_3* Hash3_Entry_Find(Hash_Entry_3* head,int url,int* Error);//查找一个链表内节点，并且返回指向那个节点的指针，如果为NULL则说明节点不存在

int Hash3_Entry_Insert(Hash_Entry_3* head,int url,int func);//插入节点，head为一个链表开头，之后查找，没找到就插入

int Hash3_Allocate(Hash_map_3* h,int size_h);//分配特定大小内存


int bucket_site(int bucket_size,const int url);//桶位置计算




Hash_Entry_3* Hash3_Entry_Creat(int key,int value)
{
    Hash_Entry_3* e=(Hash_Entry_3*)malloc(sizeof(Hash_Entry_3));
    if(e==NULL)
    {
        return NULL;
    }
    e->key=key;
    e->value=value;
    e->next=NULL;

    return e;
}

Hash_Entry_3* Hash3_Entry_Find(Hash_Entry_3* head,int url,int* Error)
{
    Hash_Entry_3* h=head;
    *Error=0;
    while(h->next!=NULL)
    {
        if(h->next->key==url)
        {
            *Error=1;
            return h;
        }
        h=h->next;
    }

    return h;
}

int Hash3_Entry_Insert(Hash_Entry_3* head,int url,int func)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Hash_Entry_3* i=Hash3_Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=Hash3_Entry_Creat(url,func);
        if(i->next!=NULL)
        {
            return 1;
        }
        else
        {
            return -1;
        }
    }

    if(i->next->key==url)
    {
        return 0;
    }
    else
    {
        return -1;
    }

    return -1;
}

int Hash3_Entry_Delete(Hash_Entry_3* head,int url)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Hash_Entry_3* i=Hash3_Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        return 1;
    }

    if(i->next->key==url)
    {
        Hash_Entry_3* m=i->next;
        i->next=m->next;
        free(m);
        return 1;
    }
    else
    {
        return -1;
    }

    return -1;
}

int Hash3_Allocate(Hash_map_3* h,int size_h)
{
    if(h->Elem!=NULL)
    {
        return 0;
    }
    h->Elem=(Hash_Entry_3*)malloc(sizeof(Hash_Entry_3)*size_h);

    if(h->Elem==NULL)
    {
        return -1;
    }

    for(int i=0;i<size_h;i++)
    {
        h->Elem[i].key=-1;
        h->Elem[i].value=-1;
        h->Elem[i].next=NULL;

    }

    return 1;
}

int Hash3_Init(Hash_map_3** h,int init)//1成功，0已存在，-1过程错误
{

    (*h)=(Hash_map_3*)malloc(sizeof(Hash_map_3));
    if(h==NULL)
    {
        return -1;
    }

    int bu_size=PRIME_BUCKET_SIZES[init-1];

    int a=Hash3_Allocate(*h,bu_size);
    if(a==-1)
    {
        return -1;
    }
    else if(a==0)
    {
        return 0;
    }

   (*h)->bu_num=bu_size;
   (*h)->elem_num=0;

    return 1;

}


Hash_Entry_3* Hash3_Find(Hash_map_3* h,int url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL)
    {
        *Error=0;
        return NULL;
    }

    int b_site=bucket_site(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=0;

    Hash_Entry_3* p=Hash3_Entry_Find(head,url,&e);

    if(e==1&&p->next!=NULL&&p->next->key==url)
    {
        *Error=1;
        return p->next;
    }
    else
    {
        *Error=-1;
    }

    return NULL;

}

int Hash3_Insert(Hash_map_3* h,int url,int func)
{

    int b_site=bucket_site(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=Hash3_Entry_Insert(head,url,func);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num++;

    return 1;


}

int Hash3_Delete(Hash_map_3* h,int url)
{
    int b_site=bucket_site(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=Hash3_Entry_Delete(head,url);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num--;

    return 1;
}

int bucket_site(int bucket_size,int url)
{
    return url/bucket_size;
}
