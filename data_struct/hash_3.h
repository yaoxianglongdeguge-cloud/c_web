

typedef struct Hash_Entry_3{
    
    int key;//连接号
    int value1;//下一个解析包序号
    int value2;//下一个要发包序号
    int value3;//目前最后一个要发的包的序号，当下一个要发包序号大于最后一个要发包后就代表这个连接目前没有要发的了，可以先回收指针池
    int value4;//用来判断是否发完这批包之后断开连接

    struct Hash_Entry_3* next;

}Hash_Entry_3;

typedef struct Hash_map_3
{
   int bu_num;
   int elem_num;
   Hash_Entry_3* Elem;

}Hash_map_3;

int Hash3_Init(Hash_map_3** h,int init);//初始化

int Hash3_Insert(Hash_map_3* h,int url,int value1,int value2,int value3,int value4);//插入

int Hash3_Delete(Hash_map_3* h,int url);

Hash_Entry_3* Hash3_Find(Hash_map_3* h,int url,int* Error);//查找










