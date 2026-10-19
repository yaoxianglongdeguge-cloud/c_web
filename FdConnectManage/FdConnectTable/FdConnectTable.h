#include "../../Include/Variate.h"


typedef struct Fd_Entry{
    
    int fd;

    int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
    int ser_nex_send;//下一个要发的包的序号
    int pack_in_path;//在途的包个数
    int need_wr;//是否给这个 fd 加过 EPOLLOUT（写完要记得改回只读）
    int pending;//在途窗口满时，暂存区里还有请求没投递；发完一组包后要续处理

    Request_Temp_Pool* request_temp_pool;
    Send_Pointer_Pool* send_pointer_pool;

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

