

typedef struct Hash_Entry_3{
    
    int key;
    int value;
    struct Hash_Entry_3* next;

}Hash_Entry_3;

typedef struct Hash_map_3
{
   int bu_num;
   int elem_num;
   Hash_Entry_3* Elem;

}Hash_map_3;

int Hash3_Init(Hash_map_3** h,int init);//初始化

int Hash3_Insert(Hash_map_3* h,int url,int func);//插入

int Hash3_Delete(Hash_map_3* h,int url);

Hash_Entry_3* Hash3_Find(Hash_map_3* h,int url,int* Error);//查找










