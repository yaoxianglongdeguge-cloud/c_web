typedef struct http_state{

    int h_method;//请求方法，0表示还没有找到,1 GET,2 DELETE,3 HEAD,4 OPTIONS,5 TRACE,6 CONNECT,7 POST,8 PUT,9 PATCH
    int h_rnrn;//请求头分隔符，0表示还没有找到
    int h_body_length;//请求体长度，如果为零，就说明没有请求体

}http_state;

typedef struct http_ed_store http_ed_store;

int http_state_init(http_state** h);

char* http_state_judge(http_ed_store* hs,int* error,int* error_reason);

int http_state_reset(http_ed_store* hs);