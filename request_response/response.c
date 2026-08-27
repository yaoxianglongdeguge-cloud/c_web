#include "../include.h"

int Response_body_set(Response* r,char* c)
{
    int len=strlen(c);
    void* ptr=NULL;
    int notfull=0;
    int n=Memory_Pool_alloc((Memory_Pool*)(r->pool),len/1024+1,&ptr,&notfull);
    if(notfull==0)
    {
        r->error_reason=503;
        return 0;
    }

    r->body=ptr;
    r->body_size=len;
    memcpy(r->body,c,len);
    *(r->body+len)='\0';

    return 1;
}

int Response_error_set(Response* r,int error)
{
    r->error_reason=error;
    return 1;
}

int Response_header_set(Response* r,char* key,char* value)
{
    int keylen=strlen(key);
    int valuelen=strlen(value);

    if(4+keylen+valuelen+r->headers->begin>=r->headers->end)
    {
        r->error_reason=503;
        return 0;
    }

    for(int i=0;i<keylen;i++)
    {
        r->headers->ptr[i]=key[i];
    }
    r->headers->ptr=r->headers->ptr+keylen;
    *(r->headers->ptr)=':';
    r->headers->ptr++;
    *(r->headers->ptr)=' ';
    r->headers->ptr++;
    for(int i=0;i<valuelen;i++)
    {
        r->headers->ptr[i]=value[i];
    }
    r->headers->ptr=r->headers->ptr+valuelen;
    *(r->headers->ptr)='\r';
    r->headers->ptr++;
    *(r->headers->ptr)='\n';
    r->headers->ptr++;
    *(r->headers->ptr)='\r';
    *(r->headers->ptr+1)='\n';
    *(r->headers->ptr+2)='\0';

    return 1;
}

int Response_fd_set(Response* r,int fd,off_t offset)
{
    r->send_fd=fd;
    r->offset=offset;
    return 1;
}