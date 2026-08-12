#include "http_analysis.h"
#include <stdio.h>
 #include <string.h>
#include <stdlib.h>
#include"../data_struct/hash_2.h"
#include "../memory_pool/memory_pool.h"
#include "http_ed_store.h"
#include "http_state.h"

int Http_analysis_body(Http_analysis_1* h,char* http_request);//type用来判断请求体的格式

int Http_analysis_body_1(Http_analysis_1* h,char* http_body);//x-www-form-urlencode类型

int Http_analysis_head(Http_analysis_1* h,char* http_request);



int Http_analysis_init(Http_analysis_1** h,memory_pool* pool,int h_size)
{

        (*h)=Memory_pool_alloc(pool,h_size);

        if((*h)==NULL)
        {
            return -1;
        }

        (*h)->ptr=((char*)(*h))+sizeof(Http_analysis_1);
        (*h)->end=(*h)+h_size;


    if((*h)==NULL)
    {
        return -1;
    }

    (*h)->Method=NULL;
    (*h)->Url=NULL;
    (*h)->Version=NULL;
    (*h)->Query=NULL;
    (*h)->Headers=NULL;
    (*h)->Body=NULL;
    (*h)->Error_h=0;

    return 1;
}

int Http_analysis_receive(Http_analysis_1* h,char* http_request)
{
    
    char *body_start = strstr(http_request, "\r\n\r\n");
    if (!body_start) return 0;

    *body_start = '\0';       // 切断头部
    body_start += 4;          // 跳过 \r\n\r\n

    int a1=Http_analysis_head(h,http_request);

    int a2=0;
    char* n=Hash2_Find(h->Headers,"Content-Type",&a2);
    if(a2!=1)
    {
        return -1;
    }

    int body_type=0;

    if(strcmp(n, "application/x-www-form-urlencoded") == 0)
    {
        body_type=1;
    }

    int a3=Http_analysis_body(h,body_start);
    if(a3!=1)
    {
        return -1;
    }

    return 1;

}

int Http_analysis_head(Http_analysis_1* h,char* http_head)
{
    char *save;
    char *line = strtok_r(http_head, "\r\n", &save);//切出第一行，save指向剩下的
    if (!line) return -1;

    //处理头部，放入键值对
    char* method;
    char* path;//包含url和查询条件
    char* version;
    
    sscanf(line, "%s %s %s", method, path, version);
     
    h->Headers=h->ptr;
    int e0=Hash2_Init(h,2);
    if(e0!=1)
    {
        return -1;
    }
    
    while (line != NULL) {

        line = strtok_r(NULL, "\r\n", &save);

        char* m=strchr(line,':');
        if(m)
        {
        *m='\0';
        int e1=0;
        e1=Hash2_Insert(h->Headers,h,line,m+1);
        if(e1!=0)
        {
            return -1;
        }
        }
        // 处理 line，按 : 切键和值
     }


    //处理查询条件，放入键值对
    char* path_cut=strchr(path,'?');
    char* url=NULL;
    char* query_ing=NULL;
    if(path_cut)
    {
        *path_cut='\0';
        query_ing=path_cut+1;
        url=path;
    }

    h->Method=method;
    h->Url=url;
    h->Version=version;

    h->Query=h->ptr;
    int e2=Hash2_Init(h,2);

    if(e2!=1)
    {
        return -1;
    }
    
    //开始切查询条件
    char *save2;
    char* line2 = strtok_r(query_ing, "&", &save2);


    while (line2 != NULL) {

        // 处理 line2，按 = 切键和值
        char* m=strchr(line2,'=');
        if(m)
        {
            *m='\0';
            int e1=0;
            e1=Hash2_Insert(h->Query,h,line2,m+1);
            if(e1!=0)
            {
                return -1;
            }
        }

        line2 = strtok_r(NULL, "&", &save2);
     }

     return 1;

}

int Http_analysis_body(Http_analysis_1* h,char* http_body)
{
    int e0=Http_analysis_body_1(h,http_body);
    if(e0!=1)
    {
        return -1;
    }

    return 1;
}




int Http_analysis_body_1(Http_analysis_1* h,char* http_body)
{
    h->Body=h->ptr;
    int e0=Hash2_Init(h,2);
    
    char *save2;
    char* line2 = strtok_r(http_body, "&", &save2);
    
    while (line2 != NULL) {
        
        // 处理 line2，按 = 切键和值
        
        char* m=strchr(line2,'=');
        if(m)
        {
            *m='\0';
            int e1=0;
            e1=Hash2_Insert(h->Body,h,line2,m+1);
            if(e1!=0)
            {
                return -1;
            }
        }
        
        line2 = strtok_r(NULL, "&", &save2);
    }
    
    return 1;
}

