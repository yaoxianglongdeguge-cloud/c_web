
typedef struct Http_analysis_1 Http_analysis_1;
typedef struct worker worker;

typedef struct http_packet{

    int fd;
    char* Response;
    Http_analysis_1* Request;

}http_packet;

int Http_main(int fd,worker* worker);