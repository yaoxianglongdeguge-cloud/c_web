#include "../include.h"


#define MAX_BODY_SIZE (1024 * 1024)  // 1MB
char* Method_p[9]={"GET ","DELETE ","HEAD ","OPTIONS ","TRACE ","CONNECT ","POST ","PUT ","PATCH "};

char *strnstr(const char *haystack, const char *needle, size_t n) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return (char *)haystack;
    
    for (size_t i = 0; i + needle_len <= n; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (char *)(haystack + i);
        }
    }
    return NULL;
}

char *strnstr_match(char *haystack, char *needle, size_t n) {
    return strnstr(haystack, needle, n);
}//匹配并返回指针函数，如果以后有需要，可以换成KMP算法


int http_state_init(http_state** h)
{
    (*h)=(http_state*)malloc(sizeof(http_state));

    (*h)->h_body_length=0;
    (*h)->h_method=0;
    (*h)->h_rnrn=0;

    return 1;

}

int minm(int a,int b)
{
    if(a<=b)
    {
        return a;
    }
    else
    {
        return b;
    }
    return 0;
}

char* http_state_judge(http_ed_store* hs,int* error,int* error_reason)//-1时代表不完整，需要accept,0时代表错误，1时代表对
{
    *error=0;
    *error_reason=200;
    char* target;
    
    int check_size=hs->ptr_e-hs->ptr_b;
    int pipei_size=minm(8,check_size);//匹配方法用的长度

    if(hs->httpstate->h_method==0)
    {

        char* method=NULL;
        int i=0;
        for(;i<9;i++)
        {
           method=strnstr_match(hs->begin,Method_p[i],pipei_size);
           if(method!=NULL)
           {
            break;
           }

        }

        if(method==NULL)
        {
            if(pipei_size<=7)
            {
                *error=-1;
                return NULL;
            }
            else//因为匹配的字符数不一定够一个方法的，所以需要返回字符不够出去再写一次
            {

                *error=0;
                *error_reason=400;
                return NULL;
            }
        }
        else
        { 
           hs->httpstate->h_method=i;
        }
        
    }
    if(hs->httpstate->h_rnrn==0)
    {

        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL)
        {
            *error=-1;
            return NULL;
        }
        else
        {
            *error=1;
            hs->httpstate->h_rnrn=1;
            target=rnrn-1;//\r前一个字符
        }
    }

    if(hs->httpstate->h_body_length==0)
    {
        char* body_length=strnstr_match(hs->begin,"Content-Length:",check_size);
        if(body_length==NULL)
        {
            if(hs->httpstate->h_method>=7)
            {
                *error_reason=411;
                *error=0;
                return NULL;
            }
            hs->httpstate->h_body_length=0;
        }
        else
        {
            *error=0;
            body_length=body_length+15;
            while(*body_length==' ')
            {
                body_length++;
            }

            char length_num[100];
            int i=0;
            while(body_length[i] != '\r' && body_length[i] != '\0' && i < 99)
            {
                length_num[i]=body_length[i];
                i++;
            }
            length_num[i] = '\0';


            int len=atoi(length_num);
            if(len>MAX_BODY_SIZE)
            {
                *error_reason=413;
                *error=0;
                return NULL;
            }

            target=target+4+len;
            if(target<hs->ptr_e){
                *error=1;
            }
            else
            {
                *error=-1;
            }

        }

    }

    return target;

}

int http_state_reset(http_ed_store* hs)
{
    hs->httpstate->h_body_length=0;
    hs->httpstate->h_method=0;
    hs->httpstate->h_rnrn=0;

    return 1;
}




