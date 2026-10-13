#include "../variate.h"

typedef struct Request{

    const char* const Method;
    const char* const Url;
    const char* const Version; 
    const Analysiser_Hash* const Query;
    const Analysiser_Hash* const Headers;
    const char* const Body;

}Request;


const char* const RequestGetter(Request* h,char* get1,char* get2);//2是1的子

