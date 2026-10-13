#include"../variate.h"

typedef struct Router{

    Router_Hash* Hrt;

}Router;



int RouterInit();//初始化

int RouterAppend(char* url,Handler func);//把url和函数加入哈希表

Handler RouterHandlerFind(char* url);//从哈希表中根据url查找对应处理函数
