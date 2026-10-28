#include "../Include/Include.h"

int TempPackInit(Temp_Pack** tempPack){

    (*tempPack)=malloc(sizeof(Temp_Pack));
    if((*tempPack)==NULL) return -1;

    (*tempPack)->head=0;
    (*tempPack)->tail=0;
    (*tempPack)->count=0;

    return 1;
}

//入队：1=成功，0=队列满
int TempPackPush(Temp_Pack* tempPack,int fd,uint64_t onlyId,char* h,int h_size,int error_reason,int serial){

    if(tempPack->count>=TEMP_PACK_MAX) return 0;

    Temp_Pack_Entry* e=&tempPack->e[tempPack->tail];
    e->fd=fd;
    e->onlyId=onlyId;
    e->h=h;
    e->h_size=h_size;
    e->error_reason=error_reason;
    e->serial=serial;

    tempPack->tail++;
    if(tempPack->tail>=TEMP_PACK_MAX) tempPack->tail=0;
    tempPack->count++;

    return 1;
}

//出队：1=成功，0=空
int TempPackPop(Temp_Pack* tempPack,int* fd,uint64_t* onlyId,char** h,int* h_size,int* error_reason,int* serial){

    if(tempPack->count<=0) return 0;

    Temp_Pack_Entry* e=&tempPack->e[tempPack->head];
    *fd=e->fd;
    *onlyId=e->onlyId;
    *h=e->h;
    *h_size=e->h_size;
    *error_reason=e->error_reason;
    *serial=e->serial;

    tempPack->head++;
    if(tempPack->head>=TEMP_PACK_MAX) tempPack->head=0;
    tempPack->count--;

    return 1;
}