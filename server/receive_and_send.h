typedef struct Request{

}Request;

typedef struct Response{

    char* txt;

}Response;

typedef struct worker worker; 
typedef struct http_analysis http_analysis;

int receive_and_send_main(worker* w);

int worker_to_profession(worker* w,int fd,http_analysis*h,int error_reason,int serial);