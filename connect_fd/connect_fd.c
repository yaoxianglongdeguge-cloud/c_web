#include "connect_fd.h"
#include <stdio.h>
#include <stdlib.h>
#include "../memory_pool/memory_pool.h"
#include "../http_analysis/http_ed_store.h"
#include "../send_tool/send_tool.h"



typedef struct Fd_Entry{
    
    int fd;

    int ser_fina_send;//下一个请求要打的序号，也是目前最后一个包序号的后一位
    int ser_nex_send;//下一个要发的包的序号

    http_ed_store* http_store;
    Send_tool* send_tool;


}

typedef struct Fd_Table{

}Fd_Table;


