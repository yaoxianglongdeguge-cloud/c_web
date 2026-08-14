#include <stdio.h>
#include<string.h>
#include <stdlib.h>
#include "http_state.h"
#include "http_ed_store.h"


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

char* http_state_judge(http_ed_store* hs,int* error,int* error_reason)//-1时代表不完整，需要accept,0时代表错误，1时代表对
{
    *error=0;
    *error_reason=0;
    char* target;
    
    int check_size=hs->ptr_e-hs->ptr_b;

    if(hs->httpstate->h_method==0)
    {

        
        char* method=strnstr_match(hs->begin,"GET ",8);
        if(method==NULL)
        {
            method=strnstr_match(hs->begin,"POST ",8);
        }
        if(method==NULL)
        {
        method=strnstr_match(hs->begin,"PUT ",8);
        }
        if(method==NULL)
        {
        method=strnstr_match(hs->begin,"DELETE ",8);
        }
        if(method==NULL)
        {
        method=strnstr_match(hs->begin,"HEAD ",8);
        }
        if(method==NULL)
        {
        method=strnstr_match(hs->begin,"OPTIONS ",8);
        }
        if(method==NULL)
        {
        method=strnstr_match(hs->begin,"PATCH ",8);   
        }
        
        if(method==NULL)
        {
            *error=0;
            return NULL;
        }
        else
        {
            hs->httpstate->h_method=1;
        }
        
    }
    if(hs->httpstate->h_rnrn==0)
    {

        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL)
        {
            *error=0;
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
            hs->httpstate->h_body_length=0;
        }
        else
        {
            *error=-1;
            body_length=body_length+15;
            while(body_length!=" ")
            {
                body_length++;
            }

            char* length_num=NULL;
            int i=0;
            while(length_num+i<hs->ptr_e&&length_num[i]!='\r')
            {
                length_num[i]=body_length[i];
                i++;
            }

            int len=atoi(length_num);

            target=target+4+len;
            if(target<hs->ptr_e){
                *error=1;
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




