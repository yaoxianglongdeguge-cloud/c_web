typedef struct Hash_map_3 Hash_map_3;

typedef struct Http_back_order{

    int num;
    Hash_map_3* order;

}Http_back_order;

int http_back_order_init(Http_back_order** h,int init);

int http_back_order_get(Http_back_order* h,int fd);//取到需要的连接的序号

int http_back_order_add(Http_back_order* h,int fd);

int http_back_order_insertfd(Http_back_order* h,int fd);

int http_back_order_deletefd(Http_back_order* h,int fd);