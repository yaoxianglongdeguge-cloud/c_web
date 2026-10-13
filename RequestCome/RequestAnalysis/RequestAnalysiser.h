#include "../../Include/Variate.h"

/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */

//到时候，这个表本身不在内存池，长期存在，表存储的数据放到内存池，处理一个请求有一次申请和一次释放。

typedef struct Request_Analysiser{

    int size;
    char* Method;
    char* Url;
    char* Version; 
    Analysiser_Hash* Query;
    Analysiser_Hash* Headers;
    char* Body;
    char* ptr;//下一个可用内存开头数据，用来连续存储内存和一次性释放内存
    char* end;//能分配的最远地方，主要是用来适配统一释放这个问题，这样可以直接操纵从哪里释放到哪里

}Request_Analysiser;



int RequestAnalysisInit(Request_Analysiser** h,Memory_Pool* pool,int h_size);

int RequestAnalysisFree(Request_Analysiser* h,Memory_Pool* pool);

int RequestAnalysisReceive(Request_Analysiser* h,char* http_request,int* error_reason);//传入存储位置，解析后会存进去





