
typedef struct worker{
   
    int id;
    int epfd;

    http_state* http_state;//http接收状态机
    http_ed_store* http_ed_store;//解析前暂存http包.
    memory_pool* http_pool;//解析后的http包的暂存处

    Send_tool* send_tool;//用来管理顺序指针循环队列，下一个空位，每个连接下一个要接收的包,还有每个连接最后要接收的包，方便释放指针队列
    Send_queue* send_queue;//接收已经准备好的要发的包的事件
    memory_pool* send_pool;//要发回的包的暂存处

    timer* my_timer;//断连计时器


}worker;