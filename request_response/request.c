#include "../include.h"


const char* const Request_get(Request* h,char* get1,char* get2)
{
    if(strcmp(get1,"Headers")==0)
    {
        int e0=0;
        if(get2==NULL)
        {
            return NULL;
        }

        char* Headers = AnalysiserHashFind((Analysiser_Hash*)(h->Headers),get2,&e0);
        if(e0!=1)
        {
            return NULL;
        }

        return (const char* const)Headers;
    }

    if(strcmp(get1,"Query")==0)
    {
        int e0=0;
        if(get2==NULL)
        {
            return NULL;
        }
        char* Query = AnalysiserHashFind((Analysiser_Hash*)(h->Query),get2,&e0);
        if(e0!=1)
        {
            return NULL;
        }

        return (const char* const)Query;
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
