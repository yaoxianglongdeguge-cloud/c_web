#include"../variate.h"

typedef struct route_1{

    Hash_map* Hrt;

}route_1;



int route_1_init();//初始化

int Handler_append(char* url,Handler func);//把url和函数加入哈希表

Handler Handler_Find(char* url);//从哈希表中根据url查找对应处理函数
