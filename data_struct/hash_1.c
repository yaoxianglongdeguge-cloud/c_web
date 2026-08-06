#include "hash_1.h"


Entry* Entry_Creat(char* key,uintptr_t value)
{
    Entry* e=(Entry*)malloc(sizeof(Entry));
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
            return h;
        }
        h=h->next;
    }

    *Error=1;
    return h;
}

int Entry_Insert(Entry* head,char* url,uintptr_t func)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Entry* i=Entry_Find(head,url,&e);
    
    if(i->next==NULL)
    {
        i->next=Entry_Creat(url,func);
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

int Hash_Allocate(Hash_map* h,int size_h)
{
    if(h->Elem!=NULL)
    {
        return 0;
    }
    h->Elem=(Entry*)malloc(sizeof(Entry)*size_h);

    if(h->Elem==NULL)
    {
        return -1;
    }

    for(int i=0;i<size_h;i++)
    {
        h->Elem[i].key="woc";
        h->Elem[i].value=0;
        h->Elem[i].next=NULL;

    }

    return 1;
}

Hash_map* Hash_Init(int* Error)//1成功，0已存在，-1过程错误
{
    *Error=0;
    Hash_map* h;

    h=(Hash_map*)malloc(sizeof(Hash_map));
    if(h==NULL)
    {
        *Error=-1;
    }

    int a=Hash_Allocate(h,7);
    if(a==-1)
    {
        *Error=-1;
    }
    else if(a==0)
    {
        *Error=0;
    }

    h->bu_num=7;
    h->elem_num=0;

    salt=random_salt();

    *Error=1;

    return h;

}

int Hash_Expend(Hash_map** h)
{
    Hash_map* m;
    m=(Hash_map*)malloc(sizeof(Hash_map));
    int a=Hash_Allocate(m,PRIME_BUCKET_SIZES[nidex+1]);
    if(a==-1)
    {
        return -1;
    }
    else if(a==0)
    {
        return 0;
    }
    m->bu_num=PRIME_BUCKET_SIZES[nidex+1];
    m->elem_num=(*h)->elem_num;

    nidex++;

    for(int i=0;i<(*h)->bu_num;i++)\
    {
        m->Elem[i]=(*h)->Elem[i];
    }

    free(*h);

    *h=m;



    return 1;

}

const Entry* Hash_Find(Hash_map* h,char* url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL)
    {
        Error=0;
        return NULL;
    }

    int b_site=bucket_site(h->bu_num,url);

    Entry* head=&(h->Elem[b_site]);

    int e=0;

    Entry* p=Entry_Find(head,url,&e);

    if(e==1&&p!=NULL&&strcmp(p->key,url)==0)
    {
        *Error=1;
        return p;
    }
    else
    {
        *Error=-1;
    }

    return NULL;

}

int Hash_Insert(Hash_map* h,char* url,uintptr_t func)
{

    int b_site=bucket_site(h->bu_num,url);

    Entry* head=&(h->Elem[b_site]);

    int e=Entry_Insert(head,url,func);

    if(e!=1)
    {
        return -1;
    }

    h->elem_num++;

    if(h->elem_num>h->bu_num*2)
    {
        Hash_Expend(&h);
    }

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
    return hash(url, salt) % bucket_size;
}


int home_handler(int a,int b){
return a+b;
}
int users_handler(int a,int c) {
return a-c;
}

int main(){

   
    int e=1;
    Hash_map* h=Hash_Init(&e);

    
    Hash_Insert(h, "/",(uintptr_t)home_handler);
    Hash_Insert(h, "/users", (uintptr_t)users_handler);
    Hash_Insert(h, "/users/fsa", (uintptr_t)users_handler);
    Hash_Insert(h, "/users/xsadq",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/qfwqerf",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/q",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/nnn",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/nnn/sff",(uintptr_t) users_handler);
    Hash_Insert(h, "/w",(uintptr_t)home_handler);
    Hash_Insert(h, "/usesads", (uintptr_t)users_handler);
    Hash_Insert(h, "/usegggrs/fsa", (uintptr_t)users_handler);
    Hash_Insert(h, "/uslfghers",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/qfwqe/rf",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/q/gh/",(uintptr_t) users_handler);
    Hash_Insert(h, "/users/nnnsdgd",(uintptr_t) users_handler);
    Hash_Insert(h, "/usebbvbbbrs/nnn/sff",(uintptr_t) users_handler);
  


    return 0;


}