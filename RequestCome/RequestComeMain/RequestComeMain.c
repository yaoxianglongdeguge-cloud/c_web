#include "../../Include/Include.h"

//单个请求（含 body）的字节上限：超过就直接 413，不再继续往内存里读
#define MAX_REQUEST_BYTES (1024*1024 + 64*1024)

static int RequestOverLimit(Fd_Entry* fd_ob)
{
    if(fd_ob->request_temp_pool==NULL||fd_ob->request_temp_pool->begin==NULL) return 0;

    return (fd_ob->request_temp_pool->ptr_e - fd_ob->request_temp_pool->begin) > MAX_REQUEST_BYTES;
}

int RequestComeMain(int fd,Receiver_Thread* Receiver_Thread,int ed_store_blocknum)
{

    //拿到连接对象
    Fd_Entry* fd_ob=NULL;
    FdTableFind(Receiver_Thread->fd_table,fd,&fd_ob);
    if(fd_ob==NULL) return 0;

    //本次进来先清"待续"标记，必要时下面会重新置位
    fd_ob->pending=0;

    //如果在途包比send池大则先不继续发包，防止send池循环覆盖未发数据
    if(fd_ob->pack_in_path>=ed_store_blocknum){ fd_ob->pending=1; return 1; }
    
    //如果请求暂存池不存在则分配
    if(fd_ob->request_temp_pool->begin==NULL){
        int n=RequestTempPoolAlloc(fd_ob->request_temp_pool,Receiver_Thread->store_area,2);
        if(n!=1) {  FdClose(Receiver_Thread,fd,503);  return 1;} 
    }
    
    //第一次向请求暂存池写入数据
    int r0=RequestTempPoolWrite(fd_ob->request_temp_pool,fd);//返回-1说明，没断开但是没数据
    if(r0==0) { FdClose(Receiver_Thread,fd,200); return 1; }//对端关闭连接
    if(RequestOverLimit(fd_ob)) { FdClose(Receiver_Thread,fd,413); return 1; }//请求超过上限

    //拿到连接已经发的最新包序号
    int serial=fd_ob->ser_fina_send;

    while(1)
    {
        //状态机开始检索http请求
        int state=0;
        int error_reason=200;
        char* source_end=NULL;

        //请求完整性判断
        RequestIntergrityJudge(fd_ob->request_temp_pool,&source_end,&state,&error_reason);

        //① 解析出一个完整请求
        if(state==1){

            int h_size=5+(source_end-fd_ob->request_temp_pool->begin+1);
            if(h_size>MAX_REQUEST_BYTES){ FdClose(Receiver_Thread,fd,413); return 1; }

            void* ptr=NULL;
            int notfull=0;
            MemoryPoolAlloc(Receiver_Thread->http_pool,(h_size/1024+1)*1024,&ptr,&notfull);
            if(notfull==0) { FdClose(Receiver_Thread,fd,503); return 1; }

            char* hptr=(char*)ptr;
            RequestTempPoolCopy(fd_ob->request_temp_pool,source_end,hptr);

            RequestTempPoolStateReset(fd_ob->request_temp_pool);

            fd_ob->ser_fina_send++;
            int put = ReceiverToWorker(Receiver_Thread,fd,hptr,h_size,error_reason,serial);
            if(put != 1){
                if(put == -1){ MemoryPoolFree(Receiver_Thread->http_pool,hptr); FdClose(Receiver_Thread,fd,503); return 1; }
                fd_ob->pending=1; break;
            }
            fd_ob->pack_in_path++;
            serial++;

            error_reason=200;

            //内存里可能还有流水线请求：继续解析，不再 read
            continue;
        }

        //② 缓冲满、请求还不完整：扩容（知道 Content-Length 就精确扩）+ 读一次
        else if(state==-1&&fd_ob->request_temp_pool->ptr_e==fd_ob->request_temp_pool->end){

            int need=0;
            if(fd_ob->request_temp_pool->state_machine->h_body_length>0){
                int head_end=RequestHeadEndOffset(fd_ob->request_temp_pool);
                if(head_end>0){
                    need=head_end+fd_ob->request_temp_pool->state_machine->h_body_length+1;
                }
            }

            int hadbigest=0;
            if(RequestTempPoolExpendTo(fd_ob->request_temp_pool,Receiver_Thread->store_area,need,&hadbigest)!=1){
                FdClose(Receiver_Thread,fd,503);
                return 1;
            }

            int r1=RequestTempPoolWrite(fd_ob->request_temp_pool,fd);
            if(r1==0){ FdClose(Receiver_Thread,fd,200); return 1; }   //对端关闭
            if(r1==-1) return 1;                                     //没数据：等下次事件，别连环扩
            if(RequestOverLimit(fd_ob)){ FdClose(Receiver_Thread,fd,413); return 1; }

            continue;
        }

        //③ 发生了错误
        else if(state==0){

            fd_ob->ser_fina_send++;
            ReceiverToWorker(Receiver_Thread,fd,NULL,0,error_reason,serial);
            fd_ob->pack_in_path++;
            error_reason=200;
            serial++;

            break;
        }

        //④ 还不够一个请求：本次事件不再读，等下一次 EPOLLIN（水平触发会再通知）
        else{

            if(fd_ob->request_temp_pool->ptr_b==fd_ob->request_temp_pool->ptr_e){
                RequestTempPoolFree(fd_ob->request_temp_pool,Receiver_Thread->store_area);
            }
            break;
        }
    }

    return 1;

}



