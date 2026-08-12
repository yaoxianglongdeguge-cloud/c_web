#include <stdio.h>
#include <sys/socket.h> 
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <unistd.h> 
#include "../router/route.h"


typedef struct Web_Driver_1{
    int server_fd;
    int error_w;
    route_1* Router;

}Web_Driver_1;

int Open_Listen(char* IP,int PORT);
