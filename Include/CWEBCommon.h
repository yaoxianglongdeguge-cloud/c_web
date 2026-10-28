#ifndef CWEB_COMMON_H
#define CWEB_COMMON_H

#define CWEB_ARRAY_LEN(a)        ((int)(sizeof(a)/sizeof((a)[0])))
#define CWEB_ALLOC_KB(n)         ((int)((((n)+1024L)/1024L)*1024L))

#define HTTP_HDR_CONTENT_LENGTH  "Content-Length"
#define HTTP_HDR_CL_FIELD        HTTP_HDR_CONTENT_LENGTH ":"

#endif
