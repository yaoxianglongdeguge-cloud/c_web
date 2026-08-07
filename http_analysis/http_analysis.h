#include <stdio.h>


/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */


typedef struct Http_analysis_1{

    char* Method;
    char* Url;
    char* Version;
    char* Host;
    char* User_Agent;
    char* Accept;
    int Error_h;

}Http_analysis_1;

int Http_analysis_init(Http_analysis_1* h);//我认为可以分配一次多次利用不释放。

int Http_analysis_operate(Http_analysis_1* h,char* http_request)
{
    

}



