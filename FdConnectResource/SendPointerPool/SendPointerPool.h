#include "../../Include/Variate.h"

typedef struct Send_Pointer_Pool_Entry{

    Worker_Queue*m_queue;  
    int use;
    int error_reason;
    char* ptr;
    int size_resp;
    int off;
    int send_fd;
    off_t offset;
    off_t size_file;

}Send_Pointer_Pool_Entry;

typedef struct Send_Pointer_Pool{

    Send_Pointer_Pool_Entry* store;
    int blocknum;

}Send_Pointer_Pool;

 //由于我们假设发回包的速度比接收返回包更快，
//所以指针队列不会满，所以说那个下一个要发挥哪一个不需要特别一个变量注明，只需要根据此时指向的数组位置有没有要发的就可以

int SendPointerPoolInit(Send_Pointer_Pool** s);

int SendPointerPoolAlloc(Send_Pointer_Pool* s,Memory_Pool* pool,int blocknum);

int SendPointerPoolFree(Send_Pointer_Pool* s,Memory_Pool* pool);

int SendPointerPoolDestory(Send_Pointer_Pool* s,Memory_Pool* pool);