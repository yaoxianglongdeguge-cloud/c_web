#include "connect_fd.h"
#include <stdio.h>
#include <stdlib.h>
#include "../my_thread/worker_thread.h"
#include "../memory_pool/memory_pool.h"
#include "../http_analysis/http_ed_store.h"
#include "../send_tool/send_tool.h"
#include "../data_struct/hash_3.h"



int Fd_Table_init(Fd_Table** f,int init)
{
    Hash3_Init(f,init);

    return 1;
}

int Fd_Table_insert(Fd_Table* f,int fd)
{
    Hash3_Insert(f,fd);
    Hash_Entry_3* ptr=NULL;
    Hash3_Find(f,fd,&ptr);
    Http_ed_store_init(&(ptr->http_store));
    send_tool_init(&(ptr->send_tool));

    return 1;
}

int Fd_Table_delete(Fd_Table* f,int fd)
{
    Hash3_Delete(f,fd);
    return 1;
}

int Fd_Table_find(Fd_Table* f,int fd,Fd_Entry** ptr)
{
    int e0=Hash3_Find(f,fd,ptr);
    if(e0!=1)
    {
        return 0;
    }
    return 1;
}