

//路由注册专用哈希表

typedef struct Request Request;
typedef struct Response Response;

typedef void (*Handler)(Request *, Response *);//路由后处理函数类型


typedef struct Entry{
    
    char* key;//存url路由
    Handler value;//存函数指针
    struct Entry* next;//指向下一个元素

}Entry;

typedef struct Hash_map
{
   int bu_num;
   int elem_num;
   Entry* Elem;

}Hash_map;

Hash_map* Hash_Init(int* Error,int init);//初始化

int Hash_Insert(Hash_map**hm,Hash_map* h,char* url,Handler func);//插入

Entry* Hash_Find(Hash_map* h,char* url,int* Error);//查找




//如果要插入元素，就先调用插入表函数，然后算桶位置，根据找到的桶头指针调用链表节点插入函数，链表节点插入函数调用链表节点查找函数.
//表插入函数不调用表查找函数，因为表查找函数返回的是只读。主要为了隔绝表外部查找操作意外修改。
//并且这个表和进程生命周期相同，所以没有特意回收内存.

//由于路由表是服务器启动前注册好的静态的，所以删除和缩容等操作就没必要做了










