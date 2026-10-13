#include "../../Include/Variate.h"

//路由注册专用哈希表

typedef struct Router_Hash_Entry{
    
    char* key;//存url路由
    Handler value;//存函数指针
    struct Router_Hash_Entry* next;//指向下一个元素

}Router_Hash_Entry;

typedef struct Router_Hash
{
   int bu_num;
   int elem_num;
   Router_Hash_Entry* Elem;

}Router_Hash;

Router_Hash* RouterHashInit(int* Error,int init);//初始化

int RouterHashInsert(Router_Hash**hm,Router_Hash* h,char* url,Handler func);//插入

Router_Hash_Entry* RouterHashFind(Router_Hash* h,char* url,int* Error);//查找




//如果要插入元素，就先调用插入表函数，然后算桶位置，根据找到的桶头指针调用链表节点插入函数，链表节点插入函数调用链表节点查找函数.
//表插入函数不调用表查找函数，因为表查找函数返回的是只读。主要为了隔绝表外部查找操作意外修改。
//并且这个表和进程生命周期相同，所以没有特意回收内存.

//由于路由表是服务器启动前注册好的静态的，所以删除和缩容等操作就没必要做了










