#include"../include.h"

int route_1_init()
{
    int e=0;
    Router->Hrt=Hash_Init(&e,2);

    if(e!=1)
    {
        return -1;
    }

    return 1;
}

int Handler_append(char* url,Handler func)
{
   int a = Hash_Insert(&(Router->Hrt),Router->Hrt,url,func);
   if(a!=1)
   {
    return -1;
   }

   return 1;
}

Handler Handler_Find(char* url)
{
    int error=0;
    Entry* e=Hash_Find(Router->Hrt,url,&error);
    if(error!=1)
    {
        return NULL;
    }

    Handler h=e->value;

    return h;
}



