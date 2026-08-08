#include "http_analysis/http_analysis.h"


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

int Http_analysis_operate(Http_analysis_1* h,char* http_request,m_pool_1* store)
{
    char* method;
}