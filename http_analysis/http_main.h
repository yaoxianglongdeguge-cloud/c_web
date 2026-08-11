typedef struct http_packet{

    int fd;
    char* Response;
    Http_analysis_1* Request;

}http_packet;