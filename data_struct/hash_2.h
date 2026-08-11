//内存池分配专用哈希表


typedef struct Http_analysis_1 Http_analysis_1;

typedef struct Entry Hash2_Entry_2;

typedef struct Hash_map
{
   int bu_num;
   int elem_num;
   Hash2_Entry_2* Elem;

}Hash_map_2;


int Hash2_Init(Http_analysis_1* Http, int init);//初始化.但这个最开始的哈希表没必要放到内存池，因为是这样，先是一个指针h，指向一个哈希表，这个表是用来存储本身信息和指向
//键值对块的指针的，而存储键值对块的内存块放到内存池里。因为需要时常修改。而这个表本身大小和位置不会变，生命周期也很长，所以没必要放到内存池，每次和其他的一起注册。

int Hash2_Insert(Hash_map_2* h,Http_analysis_1* Http, char* url,void* func);//插入.要指明用哪个类型内存池

char* Hash2_Find(Hash_map_2* h,char* url,int* Error);//查找

int Hash2_Free(Hash_map_2* h,int type,void* pool);



//如果要插入元素，就先调用插入表函数，然后算桶位置，根据找到的桶头指针调用链表节点插入函数，链表节点插入函数调用链表节点查找函数.
//表插入函数不调用表查找函数，因为表查找函数返回的是只读。主要为了隔绝表外部查找操作意外修改。
//并且这个表和进程生命周期相同，所以没有特意回收内存.











