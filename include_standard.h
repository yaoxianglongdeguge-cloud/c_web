#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include<stdint.h>
#include <time.h>
#include <pthread.h> 
#include <semaphore.h> 
#include <arpa/inet.h>   
#include <unistd.h> 
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <signal.h>
