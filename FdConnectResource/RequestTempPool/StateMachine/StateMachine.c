#include "../../../include.h"

int StateMachineInit(State_Machine** h)
{
    (*h)=(State_Machine*)malloc(sizeof(State_Machine));

    (*h)->h_body_length=0;
    (*h)->h_method=0;
    (*h)->h_rnrn=0;

    return 1;

}





