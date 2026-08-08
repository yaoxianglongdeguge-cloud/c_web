#include "http_analysis/http_analysis.h"


int Http_analysis_body(Http_analysis_1* h,char* http_request,m_pool_1* store,int type);//type用来判断请求体的格式

int Http_analysis_body_1();//x-www-form-urlencode类型

int Http_analysis_head(Http_analysis_1* h,char* http_request,m_pool_1* store);


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
    h->Query=NULL;
    h->Headers=NULL;
    h->Body=NULL;
    h->Error_h=0;

    return 1;
}

int Http_analysis_receive(Http_analysis_1* h,char* http_request,m_pool_1* store)
{
    char *body_start = strstr(http_request, "\r\n\r\n");
    if (!body_start) return 0;

    *body_start = '\0';       // 切断头部
    body_start += 4;          // 跳过 \r\n\r\n

    int a1=Http_analysis_head(h,http_request,store);



}



int Http_analysis_head(Http_analysis_1* h,char* http_head,m_pool_1* store)
{
    char *save;
    char *line = strtok_r(http_head, "\r\n", &save);//切出第一行，save指向剩下的
    if (!line) return -1;

    char* method;
    char* path;//包含url和查询条件
    char* version;

    sscanf(line, "%s %s %s", method, path, version);

    //处理查询条件，放入键值对
    char* path_cut=strchr(path,"?");
    char* url=NULL;
    char* query_ing=NULL;
    if(path_cut)
    {
        url=path_cut;
        
    }



    //处理头部，放入键值对
    while (line != NULL) {
    // 处理 line，按 : 切键和值
    line = strtok_r(NULL, "\r\n", &save);    // 下一行

     }

}

int int Http_analysis_body(Http_analysis_1* h,char* http_body,m_pool_1* store,int type)
{

}
int Http_analysis_body_1();
