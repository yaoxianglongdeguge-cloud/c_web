#include"../data_struct/hash_1.h"


typedef struct route_1{

    Hash_map* Hrt;

}route_1;

typedef struct Request Request;
typedef struct Response Response;

int route_1_init(route_1* r);//初始化

int Handler_append(route_1* r,char* url,Handler func);//把url和函数加入哈希表

Handler Handler_Find(route_1* r,char* url);//从哈希表中根据url查找对应处理函数
