#include "../include.h"

int StateMachineInit(StateMachine** h)
{
    (*h)=(StateMachine*)malloc(sizeof(StateMachine));

    (*h)->h_body_length=0;
    (*h)->h_method=0;
    (*h)->h_rnrn=0;

    return 1;

}





