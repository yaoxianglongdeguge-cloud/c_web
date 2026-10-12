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

int MethodMatch(int* sit,char** method){
    char* method=NULL;

    for(;*i<9;(*i)++){

        *method=strnstr_match(hs->begin,Method_p[i],);8
        if(*method!=NULL)
        {
            break;
        }

    }

    return 1;

}

int BodyLength(int* len,char* bodylength){
    bodylength=bodylength+15;
    while(*body_length==' ') body_length++;

    char length_num[100];
    int i=0;
    while(body_length[i] != '\r' && body_length[i] != '\0' && i < 99){

        length_num[i] = body_length[i];
        i++;
    }
    length_num[i] = '\0';

    *len=atoi(length_num);

    return 1;

}

int minm(int a,int b){
    if(a<=b)  return a;
    return b;
}

int RequestIntergrityJudge(Request_Temp_Pool* hs,char** ptr,int* fail,int* fail_reason)//-1时代表不完整，需要accept,0时代表错误，1时代表对
{
    *fail=0;
    *fail_reason=200;
    char* target;
    
    int check_size=hs->ptr_e-hs->ptr_b;
    
    //判断方法存在
    if(hs->state_machine->h_method==0){
        if(check_size<=7){  *fail=-1;  return;  }

        int i=0;
        char* method;
        MethodMatch(&i,&method);

        if(method==NULL){  *fail=0;  *fail_reason=400;  return ;  }
         
        hs->state_machine->h_method=i;     
    }

    //判断分隔符存在
    if(hs->state_machine->h_rnrn==0){

        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL){  *fail=-1;  return;  }

        *fail=1;
        hs->state_machine->h_rnrn=1;
        target=rnrn-1;//\r前一个字符
    }

    //判断请求体存在并求长度
    if(hs->state_machine->h_body_length==0){
        char* body_length=strnstr_match(hs->begin,"Content-Length:",check_size);
        if(body_length==NULL){

            hs->state_machine->h_body_length=0;

            if(hs->state_machine->h_method>=7){  *fail_reason=411;  *fail=0;  return ;  }

        }else{
            *fail=0;
            body_length=body_length+15;
            int len=0;
            BodyLength(&len,body_length);

            if(len>MAX_BODY_SIZE){  *fail_reason=413;  *fail=0;  return ;  }

            target=target+4+len;
            if(target<hs->ptr_e){ *fail=1; } else { *fail=-1; }

        }

    }

    *ptr = target;
    return ;

}
