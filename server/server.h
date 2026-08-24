#include "../variate.h"


typedef struct Web_Driver_1{
    int server_fd;
    int error_w;
    route_1* Router;

}Web_Driver_1;

int Open_Listen(char* IP,int PORT);
