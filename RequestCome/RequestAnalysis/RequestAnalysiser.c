#include "../include.h"

#define MAX_URL_LEN 8192         // 8KB
#define MAX_HEADER_LEN 16384     // 16KB




int RequestAnalysisBody(Request_Analysiser* h,char* http_request);//type用来判断请求体的格式

int RequestAnalysisHead(Request_Analysiser* h,char* http_request,int* error_reason);



int RequestAnalysisInit(Request_Analysiser** h,Memory_Pool* pool,int h_size)//单位是字节
{
    void* ptr=NULL;
    int notfull=0;
    int e0=MemoryPoolAlloc(pool,(h_size/1024)+1,&ptr,&notfull);
    if(notfull==0)  return -1;
    
    (*h)=ptr;
    (*h)->size=1024*((h_size/1024)+1);

    if((*h)==NULL)  return -1;

    (*h)->ptr=((char*)(*h))+sizeof(Request_Analysiser);
    (*h)->end=((char*)(*h))+(*h)->size-1;

    if((*h)->ptr>=(*h)->end)  return 0;


    if((*h)==NULL)   return -1;
    

    (*h)->Method=NULL;
    (*h)->Url=NULL;
    (*h)->Version=NULL;
    (*h)->Query=NULL;
    (*h)->Headers=NULL;
    (*h)->Body=NULL;

    return 1;
}

int RequestAnalysisFree(Request_Analysiser* h,Memory_Pool* pool)
{
    MemoryPoolFree(pool,h,h->size);
    return 1;
}

int RequestAnalysisReceive(Request_Analysiser* h,char* http_request,int *error_reason)
{
    //判断请求体是否存在
    char* body_start = strstr(http_request, "\r\n\r\n");
    if (!body_start){

       int a1=RequestAnalysisHead(h,http_request,error_reason);
       if(a1!=1) return -1;

      return 1;

    }else{  
       *body_start = '\0';       // 切断头部
       body_start += 4;          // 跳过 \r\n\r\n
    } 

    int a1=RequestAnalysisHead(h,http_request,error_reason);
    if(a1!=1) return -1;
    
    int a3=RequestAnalysisBody(h,body_start);
    if(a3!=1) return -1;

    return 1;

}

int DealFirst(char* Line,char** method,char** urlAndQuery,char** version){
    char *save_p;
    *method = strtok_r(line, " ", &save_p);
    *urlAndQuery = strtok_r(NULL, " ", &save_p);
    *version= strtok_r(NULL, " ", &save_p);
    return 1;
}

int DealUrlAndQuery(Request_Analysiser* h,char* urlAndQuery,int* failReason){
    char* path_cut=strchr(urlAndQuery,'?');

    if(path_cut==NULL){

        h->Url=path;
        size_t lenu = strlen(h->Url);
        if(lenu>MAX_URL_LEN){  *error_reason=414;  return 0;  }

    }else{//要切的话首先要有查询条件，如果没有就会返回NULL，如果不特殊处理就会导致对着一个NULL指针操作，得不到url
        
        *path_cut='\0';
        h->Url=urlAndQuery;
        char* query_ing=path_cut+1;
        

        size_t lenu = strlen(h->Url);
        if(lenu>MAX_URL_LEN){  *error_reason=414;  return 0;  }
        
        //创建查询条件hash
        h->Query=(Analysiser_Hash*)h->ptr;
        int e2=AnalysiserHashInit(h,1);
        if(e2!=1){  *error_reason=400;  return -1;  }
        
        //开始切查询条件
        char *save2;
        char* line2 = "";
        do{ 
        // 处理 line2，按 = 切键和值
            char* m=strchr(line2,'=');
            if(m){
                *m='\0';
                int e1=0;
                e1=AnalysiserHashInsert(h->Query,h,line2,m+1);
                if(e1!=1){  *error_reason=400;  return -1;  }
            }
                    
            line2 = strtok_r(NULL, "&", &save2);

            }while (line2 != NULL)

        }

    return 1;
        
}

int DealHeaders(Http_analysis* h,char** save){

    h->Headers=(Analysiser_Hash*)h->ptr;
    int e0=AnalysiserHashInit(h,1);
    if(e0!=1){  *error_reason=400;  return -1;  }
    
    line = strtok_r(NULL, "\r\n", save);
    while (line != NULL) {
    
        char* m=strchr(line,':');

        if(m){
           *m='\0';
            m=m+1;
            while(*m==' ') m++;
            
            size_t lenm = strlen(m);
            if(lenm>MAX_HEADER_LEN){  *error_reason=431;  return 0;  }

            int e1=AnalysiserHashInsert(h->Headers,h,line,m);
            if(e1!=1){  *error_reason=400;  return -1;  }

        }
            // 处理 line，按 : 切键和值
        line = strtok_r(NULL, "\r\n", save);//如果放到循环上面会导致循环条件改变后无法立刻检测
    
    }
        
    return 1;
}

int RequestAnalysisHead(Request_Analysiser* h,char* http_head,int* error_reason)
{
    char *save;
    char *line = strtok_r(http_head, "\r\n", &save);//切出第一行，save指向剩下的
    if (!line) return -1;

    //处理头部，放入键值dui
    char* urlAndQuery = "";//包含url和查询条件
    DealFirst(line,&(h->Method),&urlAndQuery,&(h->Version));

    //处理url和查询条件
    DealUrlAndQuery(h,&urlAndQuery,error_reason);
        
    //处理header
    DealHeaders(h,&save);
        
    return 1;

}

int RequestAnalysisBody(Request_Analysiser* h,char* http_body)
{
    h->Body=http_body;

    return 1;
}



