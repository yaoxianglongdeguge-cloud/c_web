#include "../Include/Variate.h"

#ifndef CWEB_TEMP_PACK_CAP
#define CWEB_TEMP_PACK_CAP 1024
#endif

#ifndef CWEB_TEMP_PACK_CAP_MAX
#define CWEB_TEMP_PACK_CAP_MAX 65536
#endif

typedef struct Temp_Pack_Entry{
    char* h;
    uint64_t onlyId;
    int fd;
    int h_size;
    int error_reason;
    int serial;
}Temp_Pack_Entry;

//任务队列满时，把请求先放进这个环形队列（原来是单槽位，会被后来的覆盖 ⇒ 整条连接丢响应）
typedef struct Temp_Pack{
    int head;
    int tail;
    int count;
    int blocknum;
    Temp_Pack_Entry* e;
}Temp_Pack;

int TempPackInit(Temp_Pack** tempPack,int blocknum);

int TempPackDestroy(Temp_Pack** tempPack);

int TempPackPush(Temp_Pack* tempPack,int fd,uint64_t onlyId,char* h,int h_size,int error_reason,int serial);

int TempPackPop(Temp_Pack* tempPack,int* fd,uint64_t* onlyId,char** h,int* h_size,int* error_reason,int* serial);