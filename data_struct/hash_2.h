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

}Hash2_Entry_2;

typedef struct Hash_map
{
   int bu_num;
   int elem_num;
   Hash2_Entry_2* Elem;

}Hash_map_2;

unsigned long salt_2=0;//随机数防止攻击者发送特定信息都哈希选进一个桶里



Hash_map_2* Hash2_Init(int* Error,int init,int type,void* pool);//初始化.但这个最开始的哈希表没必要放到内存池，因为是这样，先是一个指针h，指向一个哈希表，这个表是用来存储本身信息和指向
//键值对块的指针的，而存储键值对块的内存块放到内存池里。因为需要时常修改。而这个表本身大小和位置不会变，生命周期也很长，所以没必要放到内存池，每次和其他的一起注册。

int Hash2_Insert(Hash_map_2**hm,Hash_map_2* h,char* url,void* func,int type,void* pool);//插入.要指明用哪个类型内存池

const Hash2_Entry_2* Hash2_Find(Hash_map_2* h,char* url,int* Error);//查找



//如果要插入元素，就先调用插入表函数，然后算桶位置，根据找到的桶头指针调用链表节点插入函数，链表节点插入函数调用链表节点查找函数.
//表插入函数不调用表查找函数，因为表查找函数返回的是只读。主要为了隔绝表外部查找操作意外修改。
//并且这个表和进程生命周期相同，所以没有特意回收内存.

static const int PRIME_BUCKET_SIZES[15] = {
    7,
    17,     // 起点
    31,     // 1.82x
    59,     // 1.90x
    113,    // 1.92x

};//用于哈希表扩容










