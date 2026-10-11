#include "../variate.h"


typedef struct Fd_Entry{
    
    int fd;

    int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
    int ser_nex_send;//下一个要发的包的序号
    int pack_in_path;

    http_ed_store* http_store;
    Send_tool* send_tool;

    struct Fd_Entry* next;

}Fd_Entry;


typedef struct Fd_Table
{
   int bu_num;
   int elem_num;
   Fd_Entry* Elem;

}Fd_Table;


int FdTableInit(Fd_Table** f,int init);

int FdTableInsert(Fd_Table* f,int fd);

int FdTableDelete(Fd_Table* f,int fd);

int FdTableFind(Fd_Table* f,int fd,Fd_Entry** ptr);

