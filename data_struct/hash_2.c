#include "data_struct/hash_2.h"
#include"memory_pool/memory_pool_1.h"

//type -1是在栈上，0是在堆上，经过内核态，更大的数字就是表明哪种内存池
//上面是错的，在栈上分配的函数结束就释放了，有问题

Entry* Entry_Creat(char* key,void* value,int type,void* pool)
{
    Entry* e;

    switch (type)
    {    
    case 1:
        e=M_pool_1_alloc(pool,sizeof(Entry));
        break;
    
    default:
        break;
    }
    
    if(e==NULL)
    {
        return NULL;
    }
    e->key=key;
    e->value=value;
    e->next=NULL;

    return e;
}

Entry* Entry_Find(Entry* head,char* url,int* Error)
{
    Entry* h=head;
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

int Entry_Insert(Entry* head,char* url,void* func,int type,void* pool)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Entry* i=Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=Entry_Creat(url,func,type,pool);
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

int Hash_Allocate(Hash_map* h,int size_h,int type,void* pool)
{
    if(h->Elem!=NULL)
    {
        return 0;
    }

    switch (type)
    {
    case 1:
        h->Elem=M_pool_1_alloc(pool,sizeof(Entry)*size_h);
        break;
    
    default:
        break;
    }

    if(h->Elem==NULL)
    {
        return -1;
    }

    for(int i=0;i<size_h;i++)
    {
        
        h->Elem[i].key="woc";
        h->Elem[i].value=NULL;
        h->Elem[i].next=NULL;

    }

    return 1;
}

Hash_map* Hash_Init(int* Error,int init,int type,void* pool)//1成功，0已存在，-1过程错误,init是哈希表初始大小
{
    *Error=0;
    Hash_map* h;

    h=(Hash_map*)malloc(sizeof(Hash_map));
    if(h==NULL)
    {
        *Error=-1;
    }

    int bu_size=PRIME_BUCKET_SIZES[init-1];

    int a=Hash_Allocate(h,bu_size,type,pool);
    if(a==-1)
    {
        *Error=-1;
    }
    else if(a==0)
    {
        *Error=0;
    }

    h->bu_num=bu_size;
    h->elem_num=0;

    if(salt_2==0)
    {
        salt_2=random_salt();
    }

    *Error=1;

    return h;

}

const Entry* Hash_Find(Hash_map* h,char* url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL)
    {
        *Error=0;
        return NULL;
    }

    int b_site=bucket_site(h->bu_num,url);

    Entry* head=&(h->Elem[b_site]);

    int e=0;

    Entry* p=Entry_Find(head,url,&e);

    if(e==1&&p->next!=NULL&&strcmp(p->next->key,url)==0)
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

int Hash_Insert(Hash_map**hm,Hash_map* h,char* url,void* func,int type,void* pool)
{

    int b_site=bucket_site(h->bu_num,url);

    Entry* head=&(h->Elem[b_site]);

    int e=Entry_Insert(head,url,func,type,pool);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num++;

    return 1;


}
 

static unsigned long hash(const char *str, unsigned long salt) {
    unsigned long h = 5381 ^ salt;
    int c;
    while ((c = *str++))
        h = h * 33 + c;
    return h;
}

unsigned long random_salt() {
    srand(time(NULL) ^ getpid());
    return (unsigned long)rand() * rand();
}

int bucket_site(int bucket_size,const char* url)
{
    return hash(url, salt_2) % bucket_size;
}
