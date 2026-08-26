#include "../include.h"

#define MAX_URL_LEN 8192         // 8KB
#define MAX_HEADER_LEN 16384     // 16KB




int Http_analysis_body(Http_analysis_1* h,char* http_request);//type用来判断请求体的格式

int Http_analysis_body_1(Http_analysis_1* h,char* http_body);//x-www-form-urlencode类型

int Http_analysis_head(Http_analysis_1* h,char* http_request,int* error_reason);




int Http_analysis_init(Http_analysis_1** h,Memory_Pool* pool,int h_size)//单位是字节
{
    void* ptr=NULL;
    int e0=Memory_Pool_alloc(pool,(h_size/1024)+1,&ptr);
    (*h)=ptr;
    (*h)->size=1024*((h_size/1024)+1);

    if((*h)==NULL)
    {
        return -1;
    }

    (*h)->ptr=((char*)(*h))+sizeof(Http_analysis_1);
    (*h)->end=((char*)(*h))+(*h)->size-1;

    if((*h)->ptr>=(*h)->end)
    {
        return 0;
    }


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

    return 1;
}

int Http_analysis_free(Http_analysis_1* h,Memory_Pool* pool)
{
    Memory_Pool_free(pool,h,h->size);
    return 1;
}

int Http_analysis_receive(Http_analysis_1* h,char* http_request,int *error_reason)
{
    
    char *body_start = strstr(http_request, "\r\n\r\n");
    if (!body_start) 
    {
       int a1=Http_analysis_head(h,http_request,error_reason);
       if(a1!=1)
      {
        return -1;
      }

      return 1;
    }
    else 
    {  
       *body_start = '\0';       // 切断头部
       body_start += 4;          // 跳过 \r\n\r\n
    } 

    int a1=Http_analysis_head(h,http_request,error_reason);

    if(a1!=1)
    {
        return -1;
    }


    int a3=Http_analysis_body(h,body_start);
    if(a3!=1)
    {
        return -1;
    }

    return 1;

}

int Http_analysis_head(Http_analysis_1* h,char* http_head,int* error_reason)
{
    char *save;
    char *line = strtok_r(http_head, "\r\n", &save);//切出第一行，save指向剩下的
    if (!line) return -1;

    //处理头部，放入键值对
    char* method;
    char* path;//包含url和查询条件
    char* version;
    
    char *save_p;
    method = strtok_r(line, " ", &save_p);
    path = strtok_r(NULL, " ", &save_p);
    version= strtok_r(NULL, " ", &save_p);
    
    h->Method=method;
    h->Version=version;

    //处理header
    h->Headers=(Hash_map_2*)h->ptr;
    int e0=Hash2_Init(h,2);
    if(e0!=1)
    {
        return -1;
    }

    line = strtok_r(NULL, "\r\n", &save);
    while (line != NULL) {

        
        char* m=strchr(line,':');
        if(m)
        {
            *m='\0';
            m=m+1;

            while(*m==' ')
            {
                m++;
            }

            int e1=0;
            size_t lenm = strlen(m);
            if(lenm>MAX_HEADER_LEN)
            {
                *error_reason=431;
                return 0;
            }
            e1=Hash2_Insert(h->Headers,h,line,m);
            if(e1!=1)
            {
                return -1;
            }
        }
        // 处理 line，按 : 切键和值
        line = strtok_r(NULL, "\r\n", &save);//如果放到循环上面会导致循环条件改变后无法立刻检测

    }


    //处理查询条件，放入键值对

    //处理url
    char* path_cut=strchr(path,'?');
    if(path_cut==NULL)
    {
        h->Url=path;
        size_t lenu = strlen(h->Url);
        if(lenu>MAX_URL_LEN)
        {
            *error_reason=414;
            return 0;
        }
    }
    //要切的话首先要有查询条件，如果没有就会返回NULL，如果不特殊处理就会导致对着一个NULL指针操作，得不到url
    else
    {
        char* url=NULL;
        char* query_ing=NULL;
        if(path_cut)
        {
            *path_cut='\0';
            query_ing=path_cut+1;
            url=path;
        }
         
        h->Url=url;
        size_t lenu = strlen(h->Url);
        if(lenu>MAX_URL_LEN)
        {
            *error_reason=414;
            return 0;
        }
        

        h->Query=(Hash_map_2*)h->ptr;
        int e2=Hash2_Init(h,2);
        
        if(e2!=1)
        {
            return -1;
        }
        
        //开始切查询条件
        char *save2;
        char* line2 = strtok_r(query_ing, "&", &save2);
        if(line2==NULL)
        {
            char* m=strchr(query_ing,'=');
            if(m)
                {
                    *m='\0';
                    int e1=0;
                    e1=Hash2_Insert(h->Query,h,query_ing,m+1);
                    if(e1!=1)
                    {
                        return -1;
                    }
                }
        }
        else
        {
            while (line2 != NULL) {
                
                // 处理 line2，按 = 切键和值
                char* m=strchr(line2,'=');
                if(m)
                {
                    *m='\0';
                    int e1=0;
                    e1=Hash2_Insert(h->Query,h,line2,m+1);
                    if(e1!=1)
                    {
                        return -1;
                    }
                }
                
                line2 = strtok_r(NULL, "&", &save2);
            }
        }
    }

     return 1;

}

int Http_analysis_body(Http_analysis_1* h,char* http_body)
{
    h->Body=http_body;

    return 1;
}



