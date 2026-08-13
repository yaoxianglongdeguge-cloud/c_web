typedef struct worker worker;


typedef struct Store_table{
    int cut;//指向第一个大队列;
    int* table;
    int end;//指向最后一个有效元素的下一个
}Store_table;

int ed_store_arr_init(worker* work,int small_num,int small_size,int big_num,int big_size);//注册多少条大的或小的，大的多大小的多小,单位是kb
//http解析前暂存区
int send_tool_arr_init(worker* work,int small_num,int small_size,int big_num,int big_size);//返回包的指针池


int ed_store_pool_fdget(worker* work,int fd);
int send_tool_arr_fdget(worker* work,int fd);


int ed_store_pool_fdalloc(worker* work,int fd);//如果没分配到则返回-2
int ed_store_pool_fdalloc(worker* work,int fd);

int ed_store_pool_fdchange(worker* work,int fd);//换更大的条
int send_tool_arr_fdchange(worker* work,int fd);

int ed_store_pool_fdfree(worker* work,int fd);//释放条
int send_tool_arr_fdfree(worker* work,int fd);

//由于这东西和线程生命周期是绑定的，也就是整个服务进程生命周期，所以内存回收统一用线程内存回收函数