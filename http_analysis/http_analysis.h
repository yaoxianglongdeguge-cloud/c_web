#include <stdio.h>

/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */

//到时候，这个表本身不在内存池，长期存在，表存储的数据放到内存池，处理一个请求有一次申请和一次释放。
typedef struct Http_state Http_state;

typedef struct Http_analysis_1 Http_analysis_1;

int Http_analysis_init(Http_analysis_1* h,int type,memory_pool* pool);//我认为可以分配一次多次利用不释放。

int Http_analysis_receive(Http_analysis_1* h,char* http_request,memory_pool* store);//传入存储位置，解析后会存进去

int Http_analysis_send(Http_analysis_1* h,char* http_response,memory_pool* store);




