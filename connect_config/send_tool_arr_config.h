typedef struct worker worker;
typedef struct Send_table_Entry Send_table_Entry;

typedef struct Send_table{
    
    int block_num;//条的格子数量
    Send_table_Entry* table;
    int end;//指向最后一个有效元素的下一个

}Send_table;

typedef struct Send_table_Entry{

    int fd;
    sem_t sem;
    pthread_mutex_t mutex;

}Send_table_Entry;

int send_tool_arr_init(worker* work,int num,int size);//返回包的指针池

int send_tool_arr_fdget(worker* work,int fd);

int send_tool_arr_fdalloc(worker* work,int fd);

int send_tool_arr_fdfree(worker* work,int fd);