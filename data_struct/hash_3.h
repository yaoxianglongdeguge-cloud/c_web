#include "../variate.h"


typedef struct Hash_Entry_3{
    
    int fd;

    int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
    int ser_nex_send;//下一个要发的包的序号
    int pack_in_path;

    http_ed_store* http_store;
    Send_tool* send_tool;

    struct Hash_Entry_3* next;

}Hash_Entry_3;


typedef struct Hash_map_3
{
   int bu_num;
   int elem_num;
   Hash_Entry_3* Elem;

}Hash_map_3;


int Hash3_Init(Hash_map_3** h,int init);//初始化

int Hash3_Insert(Hash_map_3* h,int url);//插入

int Hash3_Delete(Hash_map_3* h,int url);

int Hash3_Find(Hash_map_3* h,int url,Hash_Entry_3** ptr);//查找










