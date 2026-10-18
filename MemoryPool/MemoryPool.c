#include "../Include/Include.h"


//初始化，分配的单位统一为字节

int MemoryEntryInit(Memory_Pool* p){

    for(int i = 0;i<p->entryNum;i++){
        char* beginPtr = p->beginPtr + i * p->perSize;
        p->manageEntry[i].beginPtr = beginPtr;
        p->manageEntry[i].allocPtr = p->manageEntry[i].beginPtr;
        p->manageEntry[i].freeSize = p->perSize;
        p->manageEntry[i].totalSize = p->perSize;
        p->manageEntry[i].hadAllocNum = 0;
    }
    
    return 1;
}


int MemoryEntryAlloc(Memory_Entry* e,void** ptr,bool* canPutInto,int needSize,bool* entryHadFull){
    int tailSize = 4;//防止越界读写在尾部预留的空间
    int totalSize = needSize + tailSize;
    *canPutInto = false;
    if(e->freeSize < 1 + tailSize){//剩余不足“1字节数据 + 护栏” → 这个池子再也不可能被用上，视为满
        *entryHadFull = true;
        return 1;
    }
    if(totalSize > e->freeSize){//这次装不下，但池子没满 → 留给后面更小的请求（关键：不能标记为满）
        *entryHadFull = false;
        return 1;
    }

    *ptr =(void*)(e->allocPtr);
    e->allocPtr = e->allocPtr + totalSize;
    e->freeSize = e->freeSize - totalSize;
    e->hadAllocNum++;
    *canPutInto = true;
    *entryHadFull = (e->freeSize < 1 + tailSize);//装完刚好不够下一个块也算满（成功路径也必须写出参）

    return 1;
}

int MemoryEntryFree(Memory_Entry* e,bool* Cleaned){

    e->hadAllocNum--;
    if(e->hadAllocNum == 0){
        *Cleaned = true;
        e->allocPtr = e->beginPtr;
        e->freeSize = e->totalSize;
    }
    
    return 1;
}



int MemoryPoolInit(Memory_Pool** p,int perSize,int entryNum){
     if(perSize <= 0 || (perSize & (perSize - 1)) != 0) return 0;  


    (*p) = malloc(sizeof(Memory_Pool));
    int totalSize = perSize * entryNum + 4;
    (*p)->beginPtr = malloc(totalSize);
    (*p)->endPtr = (*p)->beginPtr + totalSize;
    (*p)->entryNum = entryNum;
    (*p)->perSize = perSize;
    (*p)->manageEntry = malloc(entryNum*sizeof(Memory_Entry));
    (*p)->hint = 0;
    MemoryEntryInit(*p);
    (*p)->freeEntry = malloc(entryNum*sizeof(bool));
    for(int i=0;i < entryNum;i++){
        (*p)->freeEntry[i] = true;
    }

    return 1;
}

int MemoryPoolAlloc(Memory_Pool* p,int needSize,void** ptr,int* notfull)
{
    const int totalSize = needSize + 4;
    *notfull = 0;
    
    int h = p->hint;
    if(h >= 0 && h < p->entryNum && p->freeEntry[h] == true &&
       p->manageEntry[h].freeSize >= totalSize){
        Memory_Entry* e = &p->manageEntry[h];
        *ptr = (void*)e->allocPtr;
       e->allocPtr += totalSize;
       e->freeSize -= totalSize;
       e->hadAllocNum++;
       p->freeEntry[h] = (e->freeSize >= 1 + 4);//剩余不足以再来一个最小块才算满
       *notfull = 1;
       return 1;
   }
 
   bool isFull = true;
    for(int i = 0;i < p->entryNum;i++){
        if(p->freeEntry[i] == true){
            bool canPutInto = false;
            bool entryHadFull = true;
            MemoryEntryAlloc(&(p->manageEntry[i]),ptr,&canPutInto,needSize,&entryHadFull);
            if(entryHadFull == true) p->freeEntry[i] = false;
            if(canPutInto == true){
                isFull = false;
                *notfull = 1;
                p->hint = i;
                break;
            }
            
        }
    }
    
    if(isFull == true) *notfull = 0;

    return 1;
}

int MemoryPoolFree(Memory_Pool* p,void* ptr){
    if(ptr == NULL) return 1;
    char* pt = (char*)ptr;
    if(pt >= p->endPtr || pt < p->beginPtr) return 0;

    int perSize = p->perSize;
    int gapSize = pt - p->beginPtr;
    int whichEntry = gapSize >> __builtin_ctz((unsigned)perSize);
    Memory_Entry* e = &(p->manageEntry[whichEntry]);
    bool Cleaned = false;
    MemoryEntryFree(e,&Cleaned);
    if(Cleaned == true) p->freeEntry[whichEntry] = true;

    return 1;

}

