#include "../Include/Include.h"

int TempPackInit(Temp_Pack** tempPack,int blocknum){

    if(tempPack==NULL) return -1;

    *tempPack=NULL;

    if(blocknum<=0) return -1;
    if(blocknum>CWEB_TEMP_PACK_CAP_MAX) blocknum=CWEB_TEMP_PACK_CAP_MAX;

    (*tempPack)=(Temp_Pack*)calloc(1,sizeof(Temp_Pack));
    if((*tempPack)==NULL) return -1;

    (*tempPack)->e=(Temp_Pack_Entry*)calloc((size_t)blocknum,sizeof(Temp_Pack_Entry));
    if((*tempPack)->e==NULL){
        free(*tempPack);
        *tempPack=NULL;
        return -1;
    }

    (*tempPack)->blocknum=blocknum;

    return 1;
}

int TempPackDestroy(Temp_Pack** tempPack){

    if(tempPack==NULL||*tempPack==NULL) return 1;

    free((*tempPack)->e);
    free(*tempPack);
    *tempPack=NULL;

    return 1;
}

//入队：1=成功，0=队列满
int TempPackPush(Temp_Pack* tempPack,int fd,uint64_t onlyId,char* h,int h_size,int error_reason,int serial){

    if(tempPack->count>=tempPack->blocknum) return 0;

    Temp_Pack_Entry* e=&tempPack->e[tempPack->tail];
    e->fd=fd;
    e->onlyId=onlyId;
    e->h=h;
    e->h_size=h_size;
    e->error_reason=error_reason;
    e->serial=serial;

    tempPack->tail++;
    if(tempPack->tail>=tempPack->blocknum) tempPack->tail=0;
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
    if(tempPack->head>=tempPack->blocknum) tempPack->head=0;
    tempPack->count--;

    return 1;
}