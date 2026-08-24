#include "../variate.h"

typedef struct Response{

    int error_reason;
    Response_Header_Entry* headers;
    char* body;

}Response; 