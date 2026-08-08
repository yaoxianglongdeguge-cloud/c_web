#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include"data_struct/hash_2.h"
#include"memory_pool/memory_pool_1.h"

/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */

//到时候，这个表本身不在内存池，长期存在，表存储的数据放到内存池，处理一个请求有一次申请和一次释放。
typedef struct Http_analysis_1{

    char* Method;
    char* Url;
    char* Version;
    Hash_map_2* Query;
    Hash_map_2* Headers;
    Hash_map_2* Body;
    int Error_h;

}Http_analysis_1;

int Http_analysis_init(Http_analysis_1* h);//我认为可以分配一次多次利用不释放。

int Http_analysis_receive(Http_analysis_1* h,char* http_request,m_pool_1* store);//传入存储位置，解析后会存进去

int Http_analysis_send(Http_analysis_1* h,char* http_response,m_pool_1* store);




