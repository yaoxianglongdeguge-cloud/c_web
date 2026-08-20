

typedef struct Memory_Pool Memory_Pool;
typedef struct Send_thing_queue Send_thing_queue;
typedef struct timer timer;
typedef struct Http_analysis_1 Http_analysis_1;
typedef struct Hash_map_3 Fd_Table;


typedef struct worker{
   
    int id;
    int epfd;

    Fd_Table* fd_table;
    Memory_Pool* store_area;
    Memory_Pool* http_pool;
    timer* my_timer;//断连计时器
    
    Send_thing_queue* Thing_queue;//接收已经准备好的要发的包的事件



}worker;


int worker_init(worker** w,int Listen_fd,int id);

int worker_to_profession(worker* w,int fd,Http_analysis_1* h,int error_reason,int serial);

int receive_and_send_main(worker* w,int Listen_fd,int time);