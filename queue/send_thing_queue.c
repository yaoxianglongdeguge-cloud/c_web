#include "../include.h"

int Send_thing_queue_init(Send_thing_queue** sq,int blocknum)
{
    *sq=NULL;
    *sq=(Send_thing_queue*)malloc(sizeof(Send_thing_queue));
    if(*sq==NULL)
    {
        return -1;
    }

    (*sq)->queue=(Send_tq_Entry*)malloc(sizeof(Send_tq_Entry)*blocknum);
    (*sq)->begin=0;
    (*sq)->end=(*sq)->begin+blocknum;
    (*sq)->ptr_in=(*sq)->begin;
    (*sq)->ptr_out=(*sq)->begin;
    (*sq)->num=0;
    (*sq)->blocknum=blocknum;

    sem_init(&((*sq)->sem_thing_queue_notfull),0,blocknum);
    pthread_mutex_init(&((*sq)->mutex_thing),NULL);


    return 1;


}

int Send_thing_queue_push(Send_thing_queue* sq,Memory_Queue* m_queue,int fd,int serial,int error_reason,int resp_size,int file_size,char*char_ptr,int send_fd,off_t offset)
{
    sem_wait(&(sq->sem_thing_queue_notfull));
    pthread_mutex_lock(&(sq->mutex_thing));

    sq->queue[sq->ptr_in].m_queue=m_queue;
    sq->queue[sq->ptr_in].error_reason=error_reason;
    sq->queue[sq->ptr_in].fd=fd;
    sq->queue[sq->ptr_in].size_file=file_size;
    sq->queue[sq->ptr_in].size_resp=resp_size;
    sq->queue[sq->ptr_in].send_fd=send_fd;
    sq->queue[sq->ptr_in].char_ptr=char_ptr;
    sq->queue[sq->ptr_in].serial=serial;
    sq->queue[sq->ptr_in].offset=offset;

    if(sq->ptr_in+1==sq->end)
    {
        sq->ptr_in=sq->begin;
    }
    else
    {
        sq->ptr_in++;
    }

    sq->num++;
    pthread_mutex_unlock(&(sq->mutex_thing));

    return 1;
}

Send_tq_Entry Send_thing_queue_top_and_pop(Send_thing_queue* sq,int* error)//0为空
{
    pthread_mutex_lock(&(sq->mutex_thing));

    *error=0;
    Send_tq_Entry s;
    s.fd=-1;
    s.serial=-1;
    s.size_file=0;
    s.size_resp=0;
    s.send_fd=0;
    s.char_ptr=NULL;
    s.error_reason=-1;
    s.m_queue=NULL;
    s.offset=0;


    if(sq->num!=0)
    {

        s.fd=sq->queue[sq->ptr_out].fd;
        s.serial=sq->queue[sq->ptr_out].serial;
        s.size_file=sq->queue[sq->ptr_out].size_file;
        s.size_resp=sq->queue[sq->ptr_out].size_resp;
        s.send_fd=sq->queue[sq->ptr_out].send_fd;
        s.char_ptr=sq->queue[sq->ptr_out].char_ptr;
        s.error_reason=sq->queue[sq->ptr_out].error_reason;
        s.m_queue=sq->queue[sq->ptr_out].m_queue;
        s.offset=sq->queue[sq->ptr_out].offset;

        if(sq->ptr_out+1==sq->end)
        {
            sq->ptr_out=sq->begin;
        }
        else
        {
            sq->ptr_out++;
        }
        sq->num--;
        *error=1;
    }

    pthread_mutex_unlock(&(sq->mutex_thing));
    sem_post(&(sq->sem_thing_queue_notfull));
    return s;
}
