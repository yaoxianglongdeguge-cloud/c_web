#include "../../../Include/Include.h"

static const int PRIME_BUCKET_SIZES_2[15];

//type -1是在栈上，0是在堆上，经过内核态，更大的数字就是表明哪种内存池
//上面是错的，在栈上分配的函数结束就释放了，有问题


Analysiser_Hash_Entry* AnalysiserHashEntryCreate(char* key,void* value,void* ptr);//创建节点,要指明用哪个类型内存池

Analysiser_Hash_Entry* AnalysiserHashEntryFind(Analysiser_Hash_Entry* head,char* url,int* Error);//查找一个链表内节点，并且返回指向那个节点的指针，如果为NULL则说明节点不存在

int AnalysiserHashEntryInsert(Analysiser_Hash_Entry* head,char* url,void* func,void* ptr);//插入节点，head为一个链表开头，之后查找，没找到就插入.要指明用哪个类型内存池

int AnalysiserHashAlloc(Analysiser_Hash* h,int size_h,Request_Analysiser* Http);//分配特定大小内存.要指明用哪个类型内存池

static unsigned long hash_2(const char *str);//哈希函数：DJB2 + 盐值

int bucket_site_2(int bucket_size,const char* url);//桶位置计算



Analysiser_Hash_Entry* AnalysiserHashEntryCreate(char* key,void* value,void* ptr)
{
    Analysiser_Hash_Entry* e;
    e=ptr;
    
    e->key=key;
    e->value=value;
    e->next=NULL;

    return e;
}

Analysiser_Hash_Entry* AnalysiserHashEntryFind(Analysiser_Hash_Entry* head,char* url,int* Error)
{
    Analysiser_Hash_Entry* h=head;
    *Error=0;
    while(h->next!=NULL){
        if(strcmp(h->next->key,url)==0) { *Error=1; return h; }
        h=h->next;
    }

    return h;
}

int AnalysiserHashEntryInsert(Analysiser_Hash_Entry* head,char* url,void* func,void* ptr)//返回1插入成功，0已经有该节点，-1过程中失败
{
    int e;
    Analysiser_Hash_Entry* i=AnalysiserHashEntryFind(head,url,&e);
    
    if(i->next==NULL){
        i->next=AnalysiserHashEntryCreate(url,func,ptr);
        if(i->next!=NULL) return 1;
        else return -1;
    }

    if(strcmp(i->next->key,url)==0) return 0;
    else return -1;

    return -1;
}

int AnalysiserHashMemOverhead(void)
{
    int per = (int)sizeof(Analysiser_Hash) + CWEB_ANALYSISER_HASH_BUCKETS * (int)sizeof(Analysiser_Hash_Entry);

    //Query 一块 + Headers 一块 + 允许的溢出条目
    return 2 * per + g_cfg.req_max_header_fields * (int)sizeof(Analysiser_Hash_Entry);
}

int AnalysiserHashAlloc(Analysiser_Hash* h,int size_h,Request_Analysiser* Http)
{

    //先判空间，再写：避免把桶数组写到分析块外面
    if(Http->ptr + size_h * (int)sizeof(Analysiser_Hash_Entry) > Http->end) return 0;

    h->Elem=(Analysiser_Hash_Entry*)(Http->ptr);

    if(h->Elem==NULL) return -1;

    Http->ptr=Http->ptr+size_h*(sizeof(Analysiser_Hash_Entry));
    if(Http->ptr>=Http->end) return 0;

    for(int i=0;i<size_h;i++){
        
        h->Elem[i].key="woc";
        h->Elem[i].value=NULL;
        h->Elem[i].next=NULL;

    }


    return 1;
}

int AnalysiserHashInit(Request_Analysiser* Http, int init)//1成功，0已存在，-1过程错误,init是哈希表初始大小
{
    Analysiser_Hash* h;
    h=(Analysiser_Hash*)Http->ptr;

    int bu_size=PRIME_BUCKET_SIZES_2[init-1];

    //先判空间：结构体也要放得下
    if(Http->ptr + (int)sizeof(*h) > Http->end) return 0;

    Http->ptr=Http->ptr+sizeof(*h);
    if(Http->ptr>=Http->end) return 0;

    int a=AnalysiserHashAlloc(h,bu_size,Http);
    if(a==-1) return -1;
    else if(a==0) return 0;
    

    h->bu_num=bu_size;
    h->elem_num=0;


    return 1;

}

char* AnalysiserHashFind(Analysiser_Hash* h,char* url,int* Error)//-1为过程错误，用来指示中间调用函数出现错误
{
    *Error=0;
    if(h==NULL||h->Elem==NULL) { *Error=0; return NULL; }

    int b_site=bucket_site_2(h->bu_num,url);

    Analysiser_Hash_Entry* head=&(h->Elem[b_site]);

    int e=0;
    Analysiser_Hash_Entry* p=AnalysiserHashEntryFind(head,url,&e);

    if(e==1&&p->next!=NULL&&strcmp(p->next->key,url)==0) { *Error=1; return p->next->value; }
    else *Error=-1;

    return NULL;

}

int AnalysiserHashInsert(Analysiser_Hash* h,Request_Analysiser* Http, char* url,void* func)
{

    int b_site=bucket_site_2(h->bu_num,url);

    Analysiser_Hash_Entry* head=&(h->Elem[b_site]);

    //先判空间，再写：溢出条目超过上限就返回 0（上层转 400），绝不越界写
    if(Http->ptr + (int)sizeof(Analysiser_Hash_Entry) > Http->end) return 0;

    int e=AnalysiserHashEntryInsert(head,url,func,Http->ptr);
    Http->ptr=Http->ptr+sizeof(Analysiser_Hash_Entry);
    
    if(Http->ptr>=Http->end) return 0;
    
    if(e!=1) return -1;

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

