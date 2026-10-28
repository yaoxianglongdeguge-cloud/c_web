#include "../../Include/Include.h"


unsigned long salt=0;//随机数防止攻击者发送特定信息都哈希选进一个桶里

static const int PRIME_BUCKET_SIZES[] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x
    211,    // 1.87x
    409,    // 1.94x
    797,    // 1.95x
    1597,   // 2.00x
    3191,   // 2.00x
    6373,   // 2.00x
    12757,  // 2.00x
    25523,  // 2.00x
    51047,  // 2.00x
    102107, // 2.00x
};//用于哈希表扩容

static int PRIME_BUCKET_SIZES_nidex=0;


Router_Hash_Entry* RouterEntryCreate(char* key,Handler value);//创建节点

Router_Hash_Entry* RouterEntryFind(Router_Hash_Entry* head,char* url,int* Error);//查找一个链表内节点，并且返回指向那个节点的指针，如果为NULL则说明节点不存在

int RouterEntryInsert(Router_Hash_Entry* head,char* url,Handler func);//插入节点，head为一个链表开头，之后查找，没找到就插入

int RouterHashAlloc(Router_Hash* h,int size_h);//分配特定大小内存

int RouterHashExpend(Router_Hash** h);//扩容

static unsigned long hash(const char *str, unsigned long salt);//哈希函数：DJB2 + 盐值

unsigned long random_salt();//salt生成，服务器启动时生成一次

int bucket_site(int bucket_size,const char* url);//桶位置计算





Router_Hash_Entry* RouterEntryCreate(char* key,Handler value)
{
    Router_Hash_Entry* e=(Router_Hash_Entry*)malloc(sizeof(Router_Hash_Entry));
    if(e==NULL) return NULL;

    e->key=key;
    e->value=value;
    e->next=NULL;

    return e;
}

Router_Hash_Entry* RouterEntryFind(Router_Hash_Entry* head,char* url,int* Error)
{
    Router_Hash_Entry* h=head;
    *Error=0;
    while(h->next!=NULL){
        if(strcmp(h->next->key,url)==0) { *Error=1; return h; }
        h=h->next;
    }

    return h;
}

int RouterEntryInsert(Router_Hash_Entry* head,char* url,Handler func)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Router_Hash_Entry* i=RouterEntryFind(head,url,&e);
    
    if(i->next==NULL){
        i->next=RouterEntryCreate(url,func);
        if(i->next!=NULL) return 1;
        else return -1;
        
    }

    if(strcmp(i->next->key,url)==0) return 0;
    else return -1;

    return -1;
}

int RouterHashAlloc(Router_Hash* h,int size_h)
{
    if(h->Elem!=NULL) return 0;
    
    h->Elem=(Router_Hash_Entry*)malloc(sizeof(Router_Hash_Entry)*size_h);

    if(h->Elem==NULL) return -1;
    

    for(int i=0;i<size_h;i++){

        h->Elem[i].key="woc";
        h->Elem[i].value=NULL;
        h->Elem[i].next=NULL;

    }

    return 1;
}

Router_Hash* RouterHashInit(int* Error,int init)//1成功，0已存在，-1过程错误
{
    *Error=0;

    if(init<1||init>CWEB_ARRAY_LEN(PRIME_BUCKET_SIZES)){
        *Error=-1;
        return NULL;
    }

    Router_Hash* h;

    h=(Router_Hash*)malloc(sizeof(Router_Hash));
    if(h==NULL){ *Error=-1; return NULL; }
    
    h->Elem=NULL;

    int bu_size=PRIME_BUCKET_SIZES[init-1];

    h->bu_num=bu_size;
    h->elem_num=0;

    if(RouterHashAlloc(h,bu_size)!=1){
        free(h);
        *Error=-1;
        return NULL;
    }

    PRIME_BUCKET_SIZES_nidex=init-1;

    salt=random_salt();

    *Error=1;

    return h;

}

int RouterHashExpend(Router_Hash** h)
{
    int next=PRIME_BUCKET_SIZES_nidex+1;

    if(next>=CWEB_ARRAY_LEN(PRIME_BUCKET_SIZES)) return 0;

    Router_Hash* m;
    m=(Router_Hash*)malloc(sizeof(Router_Hash));
    if(m==NULL) return -1;

    m->Elem=NULL;
    m->bu_num=PRIME_BUCKET_SIZES[next];
    m->elem_num=0;

    if(RouterHashAlloc(m,PRIME_BUCKET_SIZES[next])!=1){
        free(m);
        return -1;
    }

    PRIME_BUCKET_SIZES_nidex=next;

    for(int i=0;i<(*h)->bu_num;i++){
        Router_Hash_Entry* n=(*h)->Elem[i].next;

        if(n==NULL) continue;
        else while(n!=NULL){

            char* url=n->key;
            Handler func=n->value;
            RouterHashInsert(&m,m,url,func);
            n=n->next;
        
        }
    }

    for(int i=0;i<(*h)->bu_num;i++){
        Router_Hash_Entry* m=(*h)->Elem[i].next;
        if(m!=NULL){
            while(m!=NULL){
                Router_Hash_Entry* p=m->next;
                free(m);
                m=p;
            }
            
        }
    }
    free((*h)->Elem);
    free(*h);

    *h=m;



    return 1;

}

Router_Hash_Entry* RouterHashFind(Router_Hash* h,char* url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL){  *Error=0;  return NULL;  }

    int b_site=bucket_site(h->bu_num,url);

    Router_Hash_Entry* head=&(h->Elem[b_site]);

    int e=0;

    Router_Hash_Entry* p=RouterEntryFind(head,url,&e);

    if(e==1&&p->next!=NULL&&strcmp(p->next->key,url)==0) { *Error=1;  return p->next; }
    else *Error=-1;

    return NULL;

}

int RouterHashInsert(Router_Hash**hm,Router_Hash* h,char* url,Handler func)
{

    int b_site=bucket_site(h->bu_num,url);

    Router_Hash_Entry* head=&(h->Elem[b_site]);

    int e=RouterEntryInsert(head,url,func);

    if(e!=1) return -1;

    h->elem_num++;

    if(h->elem_num>h->bu_num*2){
        static int warned=0;
        if(RouterHashExpend(hm)==0&&warned==0){
            warned=1;
            printf("[RouterHash] 路由表已达扩容上限，停止扩容（%d 桶 / %d 条）\n",h->bu_num,h->elem_num);
        }
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
