#include "../../Include/Include.h"

//epoll_ctl / fcntl 失败统一上报（限流 5 次，避免关闭竞态时刷屏）
static void EpollLogFail(const char* what,int fd)
{
    int e=errno;   //先保存：fprintf/strerror 可能改动 errno

    static int warned=0;
    if(warned<5){
        warned++;
        fprintf(stderr,"[Epoll] %s(fd=%d) 失败：errno=%d (%s)\n",
                what,fd,e,strerror(e));
    }
}

int EpollReadMount(int epfd,int fd){

    struct epoll_event ev;
    int flags = fcntl(fd, F_GETFL, 0); 
    if(flags<0||fcntl(fd, F_SETFL, flags | O_NONBLOCK)!=0)
        EpollLogFail("fcntl(O_NONBLOCK)",fd);
    ev.events = EPOLLIN; 
    ev.data.fd = fd;          
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)!=0)
        EpollLogFail("epoll_ctl ADD",fd);

    return 1;

}

int EpollChangeRead(int epfd,int fd){

    struct epoll_event ev;
    ev.events  = EPOLLIN;   
    ev.data.fd = fd;             
    if(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev)!=0)
        EpollLogFail("epoll_ctl MOD",fd);

    return 1;

}

int EpollChangeReAndWr(int epfd,int fd){

    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLOUT;   
    ev.data.fd = fd;             
    if(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev)!=0)
        EpollLogFail("epoll_ctl MOD",fd);

    return 1;

}

int EpollDelete(int epfd,int fd){

    if(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)!=0)
        EpollLogFail("epoll_ctl DEL",fd);

    return 1;

}