#include "my_rwlock_t.h"
#include<pthread.h>
#include <stdio.h>
#include <stdlib.h>


int my_rwlock_init(my_rwlock_t* rw)
{
    pthread_mutex_init(&(rw->mutex), NULL);
    pthread_cond_init(&(rw->cond), NULL);
    rw->readers = 0;
    rw->writers = 0;
    rw->waiting_writers = 0;

    return 1;
}

int my_lock_rdlock(my_rwlock_t* rw)
{
    pthread_mutex_lock(&(rw->mutex));
    while(rw->waiting_writers>0||rw->waiting_writers>0)
    {
        pthread_cond_wait(&(rw->cond),&(rw->mutex));
    }
    rw->readers++;
    pthread_mutex_unlock(&(rw->mutex));
}

int my_lock_wrlock(my_rwlock_t* rw)
{
    pthread_mutex_lock(&(rw->mutex));
    rw->waiting_writers++;
    while(rw->readers>0||rw->writers>0)
    {
        pthread_cond_wait(&(rw->cond),&(rw->mutex));
    }
    rw->waiting_writers--;
    rw->writers=1;
    pthread_mutex_unlock(&(rw->mutex));
}

int my_lock_unlock(my_rwlock_t* rw)
{
    pthread_mutex_lock(&(rw->mutex));
    if(rw->readers>0)
    {
        rw->readers--;
    }
    else if(rw->writers=1)
    {
        rw->writers=0;
    }
    pthread_cond_broadcast(&(rw->cond));
    pthread_mutex_unlock(&(rw->mutex));
}