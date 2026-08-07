#include <stdio.h>


/*> GET / HTTP/1.1          
> Host: www.baidu.com     
> User-Agent: curl/8.0
> Accept: * */


typedef Http_analysis_1{

    char* Method;
    char* Url;
    char* Version;
    char* Host;
    char* User_Agent;
    char* Accept;
    int Error_h;

}Http_analysis_1;

int Http_analysis_init(Http_analysis_1* h)//我认为可以分配一次多次利用不释放。
{
    h=(Http_analysis_1*)malloc(sizeof(Http_analysis_1));
    if(h==NULL)
    {
        return -1;
    }

    h->Method=NULL;
    h->Url=NULL;
    h->Version=NULL;
    h->Host=NULL;
    h->User_Agent=NULL;
    h->Accept=NULL;
    h->Error_h=0;

    return 1;
}



