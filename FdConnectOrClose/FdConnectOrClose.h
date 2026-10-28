#include "../Include/Variate.h"

int FdConnect(Receiver_Thread* w,int Listen_fd);

int FdClose(Receiver_Thread* w,int client_fd,int error_reason);

int FdCloseChecked(Receiver_Thread* w,int client_fd,uint64_t onlyId,int error_reason);