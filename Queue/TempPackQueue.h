#include "../Include/Variate.h"

#define TEMP_PACK_MAX 1024

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
    Temp_Pack_Entry e[TEMP_PACK_MAX];
}Temp_Pack;

int TempPackInit(Temp_Pack** tempPack);

int TempPackPush(Temp_Pack* tempPack,int fd,uint64_t onlyId,char* h,int h_size,int error_reason,int serial);

int TempPackPop(Temp_Pack* tempPack,int* fd,uint64_t* onlyId,char** h,int* h_size,int* error_reason,int* serial);