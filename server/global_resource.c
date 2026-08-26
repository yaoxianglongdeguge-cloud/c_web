#include "../include.h"

Task_queue* Task_Queue;

route_1* Router;

void Handle_1(Request* r,Response* w)
{
    int fd=open("a.jpeg",O_RDONLY);
    Response_fd_set(w,fd,0);
    Response_header_set(w,"Content_Type","image/jpeg");
}

void Handle_2(Request* r,Response* w)
{
    Response_body_set(w,"Hello world");
    Response_header_set(w,"Content-Type","text/plain");
}



int global_resource_init(int Task_Queue_blocknum)
{
    Router=(route_1*)malloc(sizeof(route_1));
    route_1_init();
    Handler_append("/yao",Handle_1);
    Handler_append("/yao/xiang",Handle_2);
    Task_queue_init(&Task_Queue,Task_Queue_blocknum);

}





