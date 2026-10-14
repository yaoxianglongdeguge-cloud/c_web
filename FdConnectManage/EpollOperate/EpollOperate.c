#include "../../Include/Include.h"

int EpollReadMount(int epfd,int fd){

    struct epoll_event ev;
    int flags = fcntl(fd, F_GETFL, 0); 
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); 
    ev.events = EPOLLIN; 
    ev.data.fd = fd;          
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

    return 1;

}

int EpollChangeRead(int epfd,int fd){

    struct epoll_event ev;
    ev.events  = EPOLLIN;   
    ev.data.fd = fd;             
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev); 

    return 1;

}

int EpollChangeReAndWr(int epfd,int fd){

    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLOUT;   
    ev.data.fd = fd;             
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev); 

    return 1;

}

int EpollDelete(int epfd,int fd){

    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

    return 1;

}