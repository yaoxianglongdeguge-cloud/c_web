#include "http_analysis/http_analysis.h"
#include <string.h>
#include <stdlib.h>
#include"data_struct/hash_2.h"
#include "memory_pool/memory_pool.h"


int Http_analysis_body(Http_analysis_1* h,char* http_request,memory_pool* store,int type);//type用来判断请求体的格式

int Http_analysis_body_1();//x-www-form-urlencode类型

int Http_analysis_head(Http_analysis_1* h,char* http_request,memory_pool* store);

typedef struct Http_analysis_1{

    char* Method;
    char* Url;
    char* Version;
    Hash_map_2* Query;
    Hash_map_2* Headers;
    Hash_map_2* Body;
    int Error_h;
    void* ptr;//下一个可用内存开头数据，用来连续存储内存和一次性释放内存
    void* end;//能分配的最远地方，主要是用来适配统一释放这个问题，这样可以直接操纵从哪里释放到哪里

}Http_analysis_1;


int Http_analysis_init(Http_analysis_1* h,int type,memory_pool* pool,int h_size)
{
    switch (type)
    {    
    case 1:
        h=Memory_pool_alloc(pool,h_size);
        if(h==NULL)
        {
            return -1;
        }
        h->ptr=h+sizeof(Http_analysis_1);
        h->end=h+h_size;
        break;
    
    default:
        break;
    }

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


int Http_analysis_receive(Http_analysis_1* h,char* http_request,memory_pool* store)
{
    char* r_end=http_request;
    *r_end='\0';
    
    char *body_start = strstr(http_request, "\r\n\r\n");
    if (!body_start) return 0;

    *body_start = '\0';       // 切断头部
    body_start += 4;          // 跳过 \r\n\r\n

    int a1=Http_analysis_head(h,http_request,store);

    int a2=0;
    char* n=Hash2_Find(h->Headers,"Content-Type",a2);
    if(a2!=1)
    {
        return -1;
    }

    int body_type=0;

    if(strcmp(n, "application/x-www-form-urlencoded") == 0)
    {
        body_type=1;
    }
    int a3=0;
    int a3=Http_analysis_body(h,body_start,store,body_type);
    if(a3!=1)
    {
        return -1;
    }

    return 1;

}

int Http_analysis_head(Http_analysis_1* h,char* http_head,memory_pool* store)
{
    char *save;
    char *line = strtok_r(http_head, "\r\n", &save);//切出第一行，save指向剩下的
    if (!line) return -1;

    //处理头部，放入键值对
    char* method;
    char* path;//包含url和查询条件
    char* version;
    
    sscanf(line, "%s %s %s", method, path, version);
    
    int e0=0;
    h->Headers=Hash2_Init(&e0,2,1,store);
    if(e0!=1)
    {
        return -1;
    }

    
    
    while (line != NULL) {

        line = strtok_r(NULL, "\r\n", &save);

        char* m=strchr(line,":");
        if(m)
        {
        *m='\0';
        int e1=0;
        e1=Hash2_Insert(&(h->Headers),h->Headers,line,m+1,1,store);
        if(e1!=0)
        {
            return -1;
        }
        }
        // 处理 line，按 : 切键和值
     }


    //处理查询条件，放入键值对
    char* path_cut=strchr(path,"?");
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

    int e2=0;
    h->Query=Hash2_Init(&e2,2,1,store);
    if(e2!=1)
    {
        return -1;
    }
    
    //开始切查询条件
    char *save2;
    char* line2 = strtok_r(query_ing, "&", &save2);


    while (line2 != NULL) {

        // 处理 line2，按 = 切键和值
        char* m=strchr(line2,"=");
        if(m)
        {
            *m='\0';
            int e1=0;
            e1=Hash2_Insert(&(h->Query),h->Query,line2,m+1,1,store);
            if(e1!=0)
            {
                return -1;
            }
        }

        line2 = strtok_r(NULL, "&", &save2);
     }

     return 1;

}

int Http_analysis_body(Http_analysis_1* h,char* http_body,memory_pool* store,int type)
{
    switch (type)
    {
    case 1:

    int e0=Http_analysis_body_1(h,http_body,store);
    if(e0!=1)
    {
        return -1;
    }

    break;
    
    default:
    break;
}
     return 1;

}

int Http_analysis_send(Http_analysis_1* h,char* http_response,memory_pool* store);

int Http_analysis_body_1(Http_analysis_1* h,char* http_body,memory_pool* store)
{
    int e0=0;
    h->Body=Hash2_Init(e0,2,1,store);

    char *save2;
    char* line2 = strtok_r(http_body, "&", &save2);

    while (line2 != NULL) {

        // 处理 line2，按 = 切键和值
        
        char* m=strchr(line2,"=");
        if(m)
        {
            *m='\0';
            int e1=0;
            e1=Hash2_Insert(&(h->Body),h->Body,line2,m+1,1,store);
            if(e1!=0)
            {
                return -1;
            }
        }

        line2 = strtok_r(NULL, "&", &save2);
     }

     return 1;
}



 