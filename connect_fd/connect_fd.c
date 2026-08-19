#include "connect_fd.h"
#include <stdio.h>
#include <stdlib.h>
#include "../memory_pool/memory_pool.h"
#include "../http_analysis/http_ed_store.h"
#include "../send_tool/send_tool.h"
#include "../data_struct/hash_3.h"


/*
typedef struct Hash_Entry_3{
    
int fd;

int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
int ser_nex_send;//下一个要发的包的序号

http_ed_store* http_store;
Send_tool* send_tool;

Hash_Entry_3* next;

}Hash_Entry_3;


typedef struct Hash_map_3
{
    int bu_num;
    int elem_num;
    Hash_Entry_3* Elem;
    
}Hash_map_3;

*/

typedef struct Hash_Entry_3 Fd_Entry;

typedef struct Hash_map_3 Fd_Table;

int Fd_Table_init(Fd_Table** f,int init)
{
    Hash3_Init(f,init);

    return 1;
}

int Fd_Table_insert(Fd_Table* f,int url)
{
    Hash3_Insert(f,url);
    return 1;
}

int Fd_Table_delete(Fd_Table* f,int url)
{
    Hash3_Delete(f,url);
    return 1;
}

int Fd_Table_find(Fd_Table* f,int url,Fd_Entry** ptr)
{
    Hash3_Find(f,url,ptr);
    return 1;
}