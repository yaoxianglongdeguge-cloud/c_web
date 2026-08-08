#include <stdio.h>
#include<stdint.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


//内存池分配专用哈希表


typedef struct Entry{
    
    char* key;//键
    void* value;//值
    struct Entry* next;//指向下一个元素

}Entry;

typedef struct Hash_map
{
   int bu_num;
   int elem_num;
   Entry* Elem;

}Hash_map;

unsigned long salt_2=0;//随机数防止攻击者发送特定信息都哈希选进一个桶里

Entry* Entry_Creat(char* key,void* value,int type,void* pool);//创建节点,要指明用哪个类型内存池

Entry* Entry_Find(Entry* head,char* url,int* Error);//查找一个链表内节点，并且返回指向那个节点的指针，如果为NULL则说明节点不存在

int Entry_Insert(Entry* head,char* url,void* func,int type,void* pool);//插入节点，head为一个链表开头，之后查找，没找到就插入.要指明用哪个类型内存池

int Hash_Allocate(Hash_map* h,int size_h,int type,void* pool);//分配特定大小内存.要指明用哪个类型内存池

Hash_map* Hash_Init(int* Error,int init,int type,void* pool);//初始化.要指明用哪个类型内存池

int Hash_Expend(Hash_map** h,int type,void* pool);//扩容.要指明用哪个类型内存池

int Hash_Insert(Hash_map**hm,Hash_map* h,char* url,void* func,int type,void* pool);//插入.要指明用哪个类型内存池

const Entry* Hash_Find(Hash_map* h,char* url,int* Error);//查找


static unsigned long hash(const char *str, unsigned long salt);//哈希函数：DJB2 + 盐值

unsigned long random_salt();//salt生成，服务器启动时生成一次

int bucket_site(int bucket_size,const char* url);//桶位置计算



//如果要插入元素，就先调用插入表函数，然后算桶位置，根据找到的桶头指针调用链表节点插入函数，链表节点插入函数调用链表节点查找函数.
//表插入函数不调用表查找函数，因为表查找函数返回的是只读。主要为了隔绝表外部查找操作意外修改。
//并且这个表和进程生命周期相同，所以没有特意回收内存.

//由于路由表是服务器启动前注册好的静态的，所以删除和缩容等操作就没必要做了

static const int PRIME_BUCKET_SIZES[15] = {
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

int PRIME_BUCKET_SIZES_nidex=0;









