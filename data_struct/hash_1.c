#include "hash_1.h"


Entry* Entry_Creat(char* key,Handler value)
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
            *Error=1;
            return h;
        }
        h=h->next;
    }

    return h;
}

int Entry_Insert(Entry* head,char* url,Handler func)//返回1插入成功，0已经有该节点，-1过程中失败
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
        h->Elem[i].value=NULL;
        h->Elem[i].next=NULL;

    }

    return 1;
}

Hash_map* Hash_Init(int* Error,int init)//1成功，0已存在，-1过程错误
{
    *Error=0;
    Hash_map* h;

    h=(Hash_map*)malloc(sizeof(Hash_map));
    if(h==NULL)
    {
        *Error=-1;
    }

    int bu_size=PRIME_BUCKET_SIZES[init-1];

    int a=Hash_Allocate(h,bu_size);
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

    salt=random_salt();

    *Error=1;

    return h;

}

int Hash_Expend(Hash_map** h)
{
    Hash_map* m;
    m=(Hash_map*)malloc(sizeof(Hash_map));
    int a=Hash_Allocate(m,PRIME_BUCKET_SIZES[PRIME_BUCKET_SIZES_nidex+1]);
    if(a==-1)
    {
        return -1;
    }
    else if(a==0)
    {
        return 0;
    }
    m->bu_num=PRIME_BUCKET_SIZES[PRIME_BUCKET_SIZES_nidex+1];
    m->elem_num=(*h)->elem_num;

    PRIME_BUCKET_SIZES_nidex++;

    for(int i=0;i<(*h)->bu_num;i++)\
    {
        Entry* n=(*h)->Elem[i].next;

        if(n==NULL)
        {
            continue;
        }
        else
        {
            while(n!=NULL)
        {

            char* url=n->key;
            Handler func=n->value;
            Hash_Insert(&m,m,url,func);
            n=n->next;

        }

        }
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

int Hash_Insert(Hash_map**hm,Hash_map* h,char* url,Handler func)
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
        Hash_Expend(hm);
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

struct Request{
    int a;
};
struct Response{
    int a;
};

void home_handler(Request * a, Response * b){
   a->a=1;
   b->a=1;
}
void users_handler(Request *a, Response * b) 
{
    a->a=1;
    b->a=1;
}

int main(){

   
    int e=1;
    Hash_map* h=Hash_Init(&e,2);

    
    Hash_Insert(&h,h, "/",home_handler);
    Hash_Insert(&h,h, "/users",users_handler);
    Hash_Insert(&h,h, "/users/fsa",users_handler);
    Hash_Insert(&h,h, "/users/xsadq",users_handler);
    Hash_Insert(&h,h, "/users/qfwqerf",users_handler);
    Hash_Insert(&h,h, "/users/q",users_handler);
    Hash_Insert(&h,h, "/users/nnn",users_handler);
    Hash_Insert(&h,h, "/users/nnn/sff", users_handler);
    Hash_Insert(&h,h, "/w",home_handler);
    Hash_Insert(&h,h, "/usesads", users_handler);
    Hash_Insert(&h,h, "/usegggrs/fsa", users_handler);
    Hash_Insert(&h,h, "/uslfghers", users_handler);
    Hash_Insert(&h,h, "/users/qfwqe/rf",users_handler);
    Hash_Insert(&h,h, "/users/q/gh/", users_handler);
    Hash_Insert(&h,h, "/users/nnnsdgd", users_handler);
    Hash_Insert(&h,h, "/usebbvbbbrs/nnn/sff", users_handler);

    Entry* m=NULL;

    m=Hash_Find(h, "/",&e);
    m=Hash_Find(h, "/users",&e);
    m=Hash_Find(h, "/users/fsa", &e);
    m=Hash_Find(h, "/users/xsadq",&e);
    m=Hash_Find(h, "/users/qfwqerf",&e);
    m=Hash_Find(h, "/users/q",&e);
    m=Hash_Find(h, "/users/nnn",&e);
    m=Hash_Find(h, "/u/nnn/sff",&e);
    m=Hash_Find(h, "/w",&e);
    m=Hash_Find(h, "/usesads", &e);
    m=Hash_Find(h, "/usegggrs/fsa",&e);
    m=Hash_Find(h, "/uslfghers",&e);
    m=Hash_Find(h, "/users/qfwqe/rf",&e);
    m=Hash_Find(h, "/users/q/gh/",&e);
    m=Hash_Find(h, "/usebbvbbbrs/nnn/sff",&e);


     Request a;
     Response b;

    Handler func=m->value;
    func(&a,&b);
    

    

  


    return 0;


}