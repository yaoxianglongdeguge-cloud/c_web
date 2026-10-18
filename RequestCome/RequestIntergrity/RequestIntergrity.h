#include "../../Include/Variate.h"

int RequestIntergrityJudge(Request_Temp_Pool* hs,char** ptr,int* fail,int* fail_reason);

//返回"\r\n\r\n"之后的偏移（= 头部总长度），找不到返回 -1
int RequestHeadEndOffset(Request_Temp_Pool* hs);