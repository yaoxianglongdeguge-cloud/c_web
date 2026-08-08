#include "server/server.h"

int Open_Listen(char* IP,int PORT);

int Open_Listen(char* IP,int PORT){

    
    int server_fd=socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr =inet_addr(IP);
    address.sin_port = htons(PORT);
    
    bind(server_fd, (struct sockaddr *)&address, sizeof(address)); 
    listen(server_fd, 10);

    return server_fd;
}