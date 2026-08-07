#include"route.h"

int route_1_init(route_1* r)
{
    r->Hrt=(Hash_map*)mallloc(sizeof(Hash_map));
    if(r->Hrt==NULL)
    {
        return -1;
    }

    return 1;
}

int Handler_append(route_1* r,char* url,Handler func)
{
   int a = Hash_Insert(&(r->Hrt),r->Hrt,url,func);
   if(a!=1)
   {
    return -1;
   }

   return 1;
}

Handler Handler_Find(route_1* r,char* url)
{
    int error=0;
    Entry* e=Hash_Find(r->Hrt,url,error);
    if(error!=1)
    {
        return NULL;
    }

    Handler h=e->value;

    return h;
}



