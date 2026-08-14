typedef struct Hash_map_3 Hash_map_3;

typedef struct Http_back_order{

    int num;
    Hash_map_3* order;

}Http_back_order;

int http_back_order_init(Http_back_order** h,int init);

int http_back_order_get(Http_back_order* h,int fd,int which);//取到需要的连接的序号

int http_back_order_add(Http_back_order* h,int fd,int which);//which是给哪个value加1,1是下一个解析包序号，2是返回包序号，3是当前最后要返回包序号
//4是需要断开连接的包的序号，可根据此序号计算出队列中位置,当然4的话就是直接写入4的错误包序号

int http_back_order_insertfd(Http_back_order* h,int fd);

int http_back_order_deletefd(Http_back_order* h,int fd);