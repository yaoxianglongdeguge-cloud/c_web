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

int Response_body_set(Response* r,char* c);

int Response_error_set(Response* r,int error);

int Response_header_set(Response* r,char* key,char* value);

int Response_fd_set(Response* r,int fd,off_t offset);


