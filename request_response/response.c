#include "../include.h"

typedef struct Response_Header_Entry{
        
    char* header;
    char* header_content;

}Response_Header_Entry;


int Response_init(Response** r,Memory_Pool* pool)
{
    void* ptr=NULL;
    Memory_Pool_alloc(pool,)
}