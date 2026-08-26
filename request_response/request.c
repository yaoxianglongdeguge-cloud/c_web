#include "../include.h"


char* Request_get(Http_analysis_1* h,char* get1,char* get2)
{
    if(strcmp(get1,"Headers")==0)
    {
        int e0=0;
        if(get2==NULL)
        {
            return NULL;
        }

        char* Headers = Hash2_Find(h->Headers,get2,&e0);
        if(e0!=1)
        {
            return NULL;
        }

        return Headers;
    }

    if(strcmp(get1,"Query")==0)
    {
        int e0=0;
        if(get2==NULL)
        {
            return NULL;
        }
        char* Query = Hash2_Find(h->Query,get2,&e0);
        if(e0!=1)
        {
            return NULL;
        }

        return Query;
    }


    if(strcmp(get1,"Url")==0)
    {
        return h->Url;
    }

    if(strcmp(get1,"Method")==0)
    {
        return h->Method;
    }

    if(strcmp(get1,"Version")==0)
    {
        return h->Version;
    }

    if(strcmp(get1,"Body")==0)
    {
        return h->Body;
    }

   return NULL;

  
}
