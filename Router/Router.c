#include "../Include/Include.h"

int RouterInit()
{
    int e=0;
    router->Hrt=RouterHashInit(&e,2);

    if(e!=1){
        fprintf(stderr,"[Router] 路由表初始化失败\n");
        return -1;
    }

    return 1;
}

int RouterAppend(char* url,Handler func)
{
   int a = RouterHashInsert(&(router->Hrt),router->Hrt,url,func);
   if(a!=1){
       //启动期注册：重复 URL 或参数非法（限流 20 次）
       static int reg_warned=0;
       if(reg_warned<20){
           reg_warned++;
           fprintf(stderr,"[Router] 注册失败（重复 URL 或参数非法）：%s\n",
                   url!=NULL?url:"(null)");
       }
       return -1;
   }

   return 1;
}

Handler RouterHandlerFind(char* url)
{
    int error=0;
    Router_Hash_Entry* e=RouterHashFind(router->Hrt,url,&error);
    if(error!=1) return NULL;

    Handler h=e->value;

    return h;
}



