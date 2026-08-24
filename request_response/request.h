#include "../variate.h"

typedef struct Request{

    const char* const Method;
    const char* const Url;
    const char* const Version; 
    const Hash_map_2* const Query;
    const Hash_map_2* const Headers;
    const char* const Body;

}Request