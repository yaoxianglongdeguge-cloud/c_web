#include "variate.h"

typedef void (*HandlerFunc)();

int Web_Driver(char* IPaddr,int PORT,HandlerFunc Handler) ;