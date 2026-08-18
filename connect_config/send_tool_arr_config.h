#include <semaphore.h> 
#include <pthread.h> 

typedef struct worker worker;
typedef struct Send_table_Entry Send_table_Entry;

typedef struct Send_table{
    
    int num;//条的数量
    int had_num;
    Send_table_Entry* table;

}Send_table;

typedef struct Send_table_Entry{

    int fd;
    int block_num;//条的格子数量
    int had_block;
    Send_tool_Entry* send_tool;

}Send_table_Entry;

int send_tool_arr_init(worker* work,int num,int size);//返回包的指针池

int send_tool_arr_fdget(worker* work,int fd);

int send_tool_arr_fdalloc(worker* work,int fd);

int send_tool_arr_fdfree(worker* work,int fd);