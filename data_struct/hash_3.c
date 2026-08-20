#include "hash_3.h"
#include <stdio.h>
#include<stdint.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../send_tool/send_tool.h"
#include "../http_analysis/http_ed_store.h"

static const int PRIME_BUCKET_SIZES_3[15] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x
 
};//用于哈希表扩容

    /*int fd;

    int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
    int ser_nex_send;//下一个要发的包的序号

    http_ed_store* http_store;
    Send_tool* send_tool; */
    
int bucket_site_3(int bucket_size,int url);

Hash_Entry_3* Hash3_Entry_Creat(int fd)
{
    Hash_Entry_3* e=(Hash_Entry_3*)malloc(sizeof(Hash_Entry_3));
    if(e==NULL)
    {
        return NULL;
    }
    e->fd=fd;
    e->ser_fina_send=0;
    e->ser_nex_send=0;
    e->pack_in_path=0;
    e->http_store=NULL;
    e->send_tool=NULL;
    e->next=NULL;

    return e;
}

Hash_Entry_3* Hash3_Entry_Find(Hash_Entry_3* head,int url,int* Error)
{
    Hash_Entry_3* h=head;
    *Error=0;
    while(h->next!=NULL)
    {
        if(h->next->fd==url)
        {
            *Error=1;
            return h;
        }
        h=h->next;
    }

    return h;
}

int Hash3_Entry_Insert(Hash_Entry_3* head,int url)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Hash_Entry_3* i=Hash3_Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=Hash3_Entry_Creat(url);
        if(i->next!=NULL)
        {
            return 1;
        }
        else
        {
            return -1;
        }
    }

    if(i->next->fd==url)
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

    if(i->next->fd==url)
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
        h->Elem[i].fd=-1;
        h->Elem[i].ser_fina_send=-1;
        h->Elem[i].ser_nex_send=-1;
        h->Elem[i].pack_in_path=-1;
        h->Elem[i].http_store=NULL;
        h->Elem[i].send_tool=NULL;
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

    int bu_size=PRIME_BUCKET_SIZES_3[init-1];

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


int Hash3_Find(Hash_map_3* h,int url,Hash_Entry_3** ptr)//-1为过程错误，用来指示中间调用函数出现错误
{
    *ptr=NULL;

    if(h==NULL||h->Elem==NULL)
    {
        return 0;
    }

    int b_site=bucket_site_3(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=0;

    Hash_Entry_3* p=Hash3_Entry_Find(head,url,&e);

    if(e==1&&p->next!=NULL&&p->next->fd==url)
    {
        *ptr=p->next;
        return 1;
    }
    else
    {
        return -1;
    }

    return 0;

}

int Hash3_Insert(Hash_map_3* h,int url)
{

    int b_site=bucket_site_3(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=Hash3_Entry_Insert(head,url);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num++;

    return 1;


}

int Hash3_Delete(Hash_map_3* h,int url)
{
    int b_site=bucket_site_3(h->bu_num,url);

    Hash_Entry_3* head=&(h->Elem[b_site]);

    int e=Hash3_Entry_Delete(head,url);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num--;

    return 1;
}

int bucket_site_3(int bucket_size,int url)
{
    return url % bucket_size;
}
