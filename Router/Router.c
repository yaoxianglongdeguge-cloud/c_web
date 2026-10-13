#include"../include.h"

int RouterInit()
{
    int e=0;
    Router->Hrt=RouterHashInit(&e,2);

    if(e!=1) return -1;

    return 1;
}

int RouterAppend(char* url,Handler func)
{
   int a = RouterHashInsert(&(Router->Hrt),Router->Hrt,url,func);
   if(a!=1) return -1;

   return 1;
}

Handler RouterHandlerFind(char* url)
{
    int error=0;
    Router_Hash_Entry* e=RouterHashFind(Router->Hrt,url,&error);
    if(error!=1) return NULL;

    Handler h=e->value;

    return h;
}



