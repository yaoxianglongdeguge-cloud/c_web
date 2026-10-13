#include "../variate.h"

typedef struct Response_Header_Entry{
        
    char* begin;
    char* ptr;
    char* end;  

}Response_Header_Entry;

typedef struct Response{

    int error_reason;
    Response_Header_Entry* headers;
    char* body;
    int body_size;
    int send_fd;
    off_t offset;
    const Memory_Pool* const pool;

}Response;

int ResponseBodySet(Response* r,char* c);

int ResponseErrorSet(Response* r,int error);

int ResponseHeaderSet(Response* r,char* key,char* value);

int ResponseFileSet(Response* r,int fd,off_t offset);


