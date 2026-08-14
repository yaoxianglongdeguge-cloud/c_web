
typedef struct Task_Entry Task_Entry;

typedef struct Task_queue{

    Task_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int size;//节点总个数

}Task_queue;

int Task_queue_init(Task_queue** sq,int size);

int Task_queue_push(Task_queue* sq,worker* w,int fd,int serial,int error_reason,Http_analysis_1* h);

Task_Entry Send_thing_queue_top_and_pop(Task_queue* sq,int* error);
