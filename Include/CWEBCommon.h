#ifndef CWEB_COMMON_H
#define CWEB_COMMON_H

#define CWEB_ARRAY_LEN(a)        ((int)(sizeof(a)/sizeof((a)[0])))
#define CWEB_ALLOC_KB(n)         ((int)((((n)+1024L)/1024L)*1024L))

#define HTTP_HDR_CONTENT_LENGTH  "Content-Length"
#define HTTP_HDR_CL_FIELD        HTTP_HDR_CONTENT_LENGTH ":"

#define HTTP_MAX_HEAD_BYTES      (64*1024)
#define HTTP_MAX_BODY_BYTES      (1024*1024)
#define HTTP_MAX_REQUEST_BYTES   (HTTP_MAX_BODY_BYTES + HTTP_MAX_HEAD_BYTES)

#endif
