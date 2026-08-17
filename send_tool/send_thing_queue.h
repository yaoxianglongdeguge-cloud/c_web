typedef struct Send_tq_Entry{

    int fd;
    int serial;

}Send_tq_Entry;

typedef struct Send_thing_queue{

    Send_tq_Entry* queue;
    int begin;
    int end;
    int ptr_in;
    int ptr_out;

    int num;//有效节点个数
    int size;//节点总个数

}Send_thing_queue;


int Send_thing_queue_init(Send_thing_queue** sq,int size);

int Send_thing_queue_push(Send_thing_queue* sq,int fd,int serial);

Send_tq_Entry Send_thing_queue_top_and_pop(Send_thing_queue* sq,int* error);