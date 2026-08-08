#include <stdio.h>
#include <sys/socket.h> 
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <unistd.h>  


int PORT=8080;

int main()
{
   int server_fd=OpenListen("127.0.0.1",8080);
   
   int client_fd;
   char store[1024]={0};

   while(1)
   {
    client_fd=accept(server_fd,NULL,NULL);
    read(client_fd,store,1024);

    printf("\n%s\n",store);

    close(client_fd);

   }


   return 0;
}