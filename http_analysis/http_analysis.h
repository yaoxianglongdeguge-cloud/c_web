

/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */

//到时候，这个表本身不在内存池，长期存在，表存储的数据放到内存池，处理一个请求有一次申请和一次释放。

typedef struct Hash_map Hash_map_2;
typedef struct memory_pool memory_pool;

typedef struct Http_analysis_1{

    char* Method;
    char* Url;
    char* Version;
    Hash_map_2* Query;
    Hash_map_2* Headers;
    char* Body;
    int Error_h;
    char* ptr;//下一个可用内存开头数据，用来连续存储内存和一次性释放内存
    char* end;//能分配的最远地方，主要是用来适配统一释放这个问题，这样可以直接操纵从哪里释放到哪里

}Http_analysis_1;



int Http_analysis_init(Http_analysis_1** h,memory_pool* pool,int h_size);

int Http_analysis_receive(Http_analysis_1* h,char* http_request);//传入存储位置，解析后会存进去

int Http_analysis_send(Http_analysis_1* h,char* http_response,memory_pool* store);

char* Http_analysis_get(Http_analysis_1* h,char* get1,char* get2);


