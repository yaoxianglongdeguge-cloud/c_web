#include <stdio.h>
#include <sys/socket.h> 
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <unistd.h> 
#include "router/route.h"


typedef struct Web_Driver_1{
    int server_fd;
    int error_w;
    route_1* Router;

}Web_Driver_1;

int Web_Driver_Main(char* IP,int PORT,route_1* ROUTE);

