#include "server.h"

Web_Driver_1* Web_Driver_Listen(char* IP,int PORT,route_1* ROUTE);


int Web_Driver_Main(char* IP,int PORT,route_1* ROUTE)
{
   Web_Driver_1* w=Web_Driver_Listen(IP,PORT,ROUTE);

   if(w==NULL)
   {
      return -1;
   }
   else
   {
      if(w->error_w!=1)
      {
         return -1;
      }
   }

   int server_fd=w->server_fd;
   Hash_map* Router=w->Router;

   while(1)
   {
      int client=accept(server_fd,NULL,NULL);
      char* store[8092];

      
      
   }

}


Web_Driver_1* Web_Driver_Listen(char* IP,int PORT,route_1* ROUTE)
{
   int server_fd=Open_Listen(IP,PORT);
   Web_Driver_1* w=(Web_Driver_1*)malloc(sizeof(Web_Driver_1));
   if(w==NULL)
   {
      return NULL;
   }

   if(server_fd==-1)
   {
      w->error_w=-1;
   }

   w->error_w=1;
   w->server_fd=server_fd;
   w->Router=ROUTE;

   return w;
}



