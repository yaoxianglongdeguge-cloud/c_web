#include "../../include.h"


static const int PRIME_BUCKET_SIZES_3[15] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x
    
};//用于哈希表扩容


Fd_Entry* FdEntryCreat(int fd);
Fd_Entry* FdEntryFind(Fd_Entry* head,int url,int* Error);
int FdEntryInsert(Fd_Entry* head,int url);
int FdEntryDelete(Fd_Entry* head,int url);
int FdTableAllocate(Fd_Table* h,int size_h);
int bucket_site_3(int bucket_size,int url);

int FdTableInit(Fd_Table** h,int init)//1成功，0已存在，-1过程错误
{

    (*h)=(Fd_Table*)malloc(sizeof(Fd_Table));
    if(h==NULL)
    {
        return -1;
    }

    int bu_size=PRIME_BUCKET_SIZES_3[init-1];

    int a=FdTableAllocate(*h,bu_size);
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


int FdTableFind(Fd_Table* h,int fd,Fd_Entry** ptr)//-1为过程错误，用来指示中间调用函数出现错误
{
    *ptr=NULL;

    if(h==NULL||h->Elem==NULL)
    {
        return 0;
    }

    int b_site=bucket_site_3(h->bu_num,fd);

    Fd_Entry* head=&(h->Elem[b_site]);

    int e=0;

    Fd_Entry* p=FdEntryFind(head,fd,&e);

    if(e==1&&p->next!=NULL&&p->next->fd==fd)
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

int FdTableInsert(Fd_Table* h,int fd)
{

    int b_site=bucket_site_3(h->bu_num,fd);

    Fd_Entry* head=&(h->Elem[b_site]);

    int e=FdEntryInsert(head,fd);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num++;

    Fd_Entry* ptr=NULL;
    FdTableFind(h,fd,&ptr);
    RequestTempPoolInit(&(ptr->request_temp_pool));
    SendPointerPoolInit(&(ptr->send_pointer_pool));

    return 1;


}

int FdTableDelete(Fd_Table* h,int fd)
{
    int b_site=bucket_site_3(h->bu_num,fd);

    Fd_Entry* head=&(h->Elem[b_site]);

    int e=FdEntryDelete(head,fd);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num--;

    return 1;
}

Fd_Entry* FdEntryCreat(int fd)
{
    Fd_Entry* e=(Fd_Entry*)malloc(sizeof(Fd_Entry));
    if(e==NULL)
    {
        return NULL;
    }
    e->fd=fd;
    e->ser_fina_send=0;
    e->ser_nex_send=0;
    e->pack_in_path=0;
    e->request_temp_pool=NULL;
    e->send_pointer_pool=NULL;
    e->next=NULL;

    return e;
}

Fd_Entry* FdEntryFind(Fd_Entry* head,int url,int* Error)
{
    
    Fd_Entry* h=head;
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

int FdEntryInsert(Fd_Entry* head,int url)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Fd_Entry* i=FdEntryFind(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=FdEntryCreat(url);
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

int FdEntryDelete(Fd_Entry* head,int url)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Fd_Entry* i=FdEntryFind(head,url,&e);
    
    if(i->next==NULL)
    {
        return 1;
    }

    if(i->next->fd==url)
    {
        Fd_Entry* m=i->next;
        i->next=m->next;
        free(m);
        return 1;
    }


    return -1;
}

int FdTableAllocate(Fd_Table* h,int size_h)
{
    h->Elem=NULL;
    h->Elem=(Fd_Entry*)malloc(sizeof(Fd_Entry)*size_h);

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
        h->Elem[i].request_temp_pool=NULL;
        h->Elem[i].send_pointer_pool=NULL;
        h->Elem[i].next=NULL;

    }

    return 1;
}

int bucket_site_3(int bucket_size,int url)
{
    return url % bucket_size;
}
