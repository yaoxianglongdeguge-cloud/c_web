#include "../../Include/Include.h"


#define MAX_BODY_SIZE (1024 * 1024)  // 1MB
char* Method_p[9]={"GET ","DELETE ","HEAD ","OPTIONS ","TRACE ","CONNECT ","POST ","PUT ","PATCH "};

int GetNextArray(const char* needle,int* nextArray,int len){
    int k=-1;
    int j=0;

    nextArray[0] = -1;
    while(j<len - 1){
        if(k==-1||needle[j] == needle[k]){
            j++;
            k++;
            nextArray[j] = k;
        }else{
            k = nextArray[k];
        }
    }

    return 1;
}

char* KMPMatch(const char* haystack, const char* needle, int n,int needle_len){

    int nextArray[needle_len];
    GetNextArray(needle,nextArray,needle_len);

    int i=0;
    int j=0;

    while( i < n && j < needle_len){
        if(j == -1 || haystack[i] == needle[j]){
            i++;
            j++;
        }else{
            j = nextArray[j];
        }
    }
    if(j >= needle_len){
        char* ptr = haystack + i - needle_len;
        return ptr;
    } 
    return NULL;

}

char* strnstr(const char *haystack, const char *needle, int n) {
    if(needle == NULL) return NULL;
    if(haystack == NULL) return NULL;
    int needle_len = (int)strlen(needle);
    if(needle_len == 0) return (char *)haystack;
    if(n < needle_len) return NULL;

    return KMPMatch(haystack,needle,n,needle_len);
}

char* strnstr_match(char *haystack, char *needle, size_t n) {
    return strnstr(haystack, needle, n);
}//匹配并返回指针函数，如果以后有需要，可以换成KMP算法


int MethodMatch(Request_Temp_Pool* hs,int* sit,char** method){
    for(;*sit<9;(*sit)++){

        *method=strnstr_match(hs->begin,Method_p[*sit],8);
        if(*method!=NULL)  break;

    }

    return 1;

}

int BodyLength(int* len,char* bodylength){
    bodylength=bodylength+15;
    while(*bodylength==' ') bodylength++;

    char length_num[100];
    int i=0;
    while(bodylength[i] != '\r' && bodylength[i] != '\0' && i < 99){

        length_num[i] = bodylength[i];
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
    char* target=NULL;  
    
    int check_size=hs->ptr_e-hs->ptr_b;
    
    //判断方法存在
    if(hs->state_machine->h_method==0){
        if(check_size<=7){  *fail=-1;  return 0;  }

        int i=0;
        char* method;
        MethodMatch(hs,&i,&method);

        if(method==NULL){  *fail=0;  *fail_reason=400;  return 0;  }
         
        hs->state_machine->h_method=i;     
    }

    //判断分隔符存在
    if(hs->state_machine->h_rnrn==0){

        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL){  *fail=-1;  return 0;  }

        *fail=1;
        hs->state_machine->h_rnrn=1;
        target=rnrn-1;//\r前一个字符
    }
    else{

        char* rnrn=strnstr_match(hs->begin,"\r\n\r\n",check_size);
        if(rnrn==NULL){  *fail=-1;  return 0;  }
        target=rnrn-1;
    }

    //判断请求体存在并求长度
    if(hs->state_machine->h_body_length==0){
        char* body_length=strnstr_match(hs->begin,"Content-Length:",check_size);
        if(body_length==NULL){

            hs->state_machine->h_body_length=0;

            if(hs->state_machine->h_method>=7){  *fail_reason=411;  *fail=0;  return 0;  }

        }else{
            *fail=0;
            int len=0;
            BodyLength(&len,body_length);

            if(len>MAX_BODY_SIZE){  *fail_reason=413;  *fail=0;  return 0;  }

            target=target+4+len;
            if(target<hs->ptr_e){ *fail=1; } else { *fail=-1; }

        }

    }

    *ptr = target;
    return 1;

}

int RequestHeadEndOffset(Request_Temp_Pool* hs)
{
    int n=hs->ptr_e-hs->begin;
    if(n<=0) return -1;

    char* p=strnstr_match(hs->begin,"\r\n\r\n",n);
    if(p==NULL) return -1;

    return (int)(p-hs->begin)+4;
}
