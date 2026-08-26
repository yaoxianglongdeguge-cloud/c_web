#include "../variate.h"

typedef struct Request{

    const char* const Method;
    const char* const Url;
    const char* const Version; 
    const Hash_map_2* const Query;
    const Hash_map_2* const Headers;
    const char* const Body;

}Request;


char* Request_get(Http_analysis_1* h,char* get1,char* get2);//2是1的子

