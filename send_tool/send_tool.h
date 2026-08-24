#include "../variate.h"

typedef struct Send_tool_Entry{

    Memory_Queue*m_queue;  
    char* ptr;
    int use;
    int error_reason;
    int size;

}Send_tool_Entry;

typedef struct Send_tool{

    Send_tool_Entry* store;
    int blocknum;

}Send_tool;

 //由于我们假设发回包的速度比接收返回包更快，
//所以指针队列不会满，所以说那个下一个要发挥哪一个不需要特别一个变量注明，只需要根据此时指向的数组位置有没有要发的就可以

int send_tool_init(Send_tool** s);

int send_tool_alloc(Send_tool* s,Memory_Pool* pool,int blocknum);

int send_tool_free(Send_tool* s,Memory_Pool* pool);

int send_tool_destory(Send_tool* s,Memory_Pool* pool);