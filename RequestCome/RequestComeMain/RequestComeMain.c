#include "../../Include/Include.h"

int RequestComeMain(int fd,Receiver_Thread* Receiver_Thread,int ed_store_blocknum)
{

    //拿到连接对象
    Fd_Entry* fd_ob=NULL;
    FdTableFind(Receiver_Thread->fd_table,fd,&fd_ob);
    if(fd_ob==NULL) return 0;

    //如果在途包比send池大则先不继续发包，防止send池循环覆盖未发数据
    if(fd_ob->pack_in_path>=ed_store_blocknum) return 1;
    
    //如果请求暂存池不存在则分配
    if(fd_ob->request_temp_pool->begin==NULL){
        int n=RequestTempPoolAlloc(fd_ob->request_temp_pool,Receiver_Thread->store_area,2);
        if(n!=1) {  FdClose(Receiver_Thread,fd,503);  return 1;} 
    }
    
    //第一次向请求暂存池写入数据
    int r0=RequestTempPoolWrite(fd_ob->request_temp_pool,fd);//返回-1说明，没断开但是没数据
    if(r0==0) { FdClose(Receiver_Thread,fd,200); return 1; }//对端关闭连接

    //拿到连接已经发的最新包序号
    int serial=fd_ob->ser_fina_send;

    while(1)
    {
                   
        //状态机开始检索http请求
        int state=0;
        int error_reason=200;
        char* source_end=NULL;

        //请求完整性判断
        RequestIntergrityJudge(fd_ob->request_temp_pool,&source_end,&state,&error_reason);//要复制的文本结尾，此处在文本中
            
        
        //不够一个请求的，但是还不需要更大的存储区
        if(state==-1&&fd_ob->request_temp_pool->ptr_e!=fd_ob->request_temp_pool->end){
            //继续把数据读到暂存区
            int r1=RequestTempPoolWrite(fd_ob->request_temp_pool,fd);
            if(r1==-1){
                //说明连接里没数据并且暂存区也没有残留数据,所以可以释放占有的暂存区
                if(fd_ob->request_temp_pool->ptr_b==fd_ob->request_temp_pool->ptr_e){
                    RequestTempPoolFree(fd_ob->request_temp_pool,Receiver_Thread->store_area);
                }
                break;
            //对面断开了连接
            }else if(r1==0){ FdClose(Receiver_Thread,fd,200); return 1; }

        }
            
        //解析出了一个完整请求
        else if(state==1){
     
            //要复制的文本长度
            int h_size=5+(source_end-fd_ob->request_temp_pool->begin+1);

            //在请求存储池申请一块内存
            void* ptr=NULL;
            int notfull=0;
            MemoryPoolAlloc(Receiver_Thread->http_pool,h_size/1024+1,&ptr,&notfull);
            if(notfull==0) { FdClose(Receiver_Thread,fd,503); return 1; }

            //把请求从暂存区拷贝到存储池
            char* hptr=(char*)ptr;
            RequestTempPoolCopy(fd_ob->request_temp_pool,source_end,hptr);

            //重置状态机
            RequestTempPoolStateReset(fd_ob->request_temp_pool);    

            //发送
            fd_ob->ser_fina_send++;
            int put = ReceiverToWorker(Receiver_Thread,fd,hptr,h_size,error_reason,serial);
            if(put != 1) break;
            fd_ob->pack_in_path++;
            serial++;

            error_reason=200;
                
        }
            
        //这个暂存区根本装不下连接发来的一个请求
        else if(state==-1&&fd_ob->request_temp_pool->ptr_e==fd_ob->request_temp_pool->end){
            int hadbigest=0;
            RequestTempPoolExpend(fd_ob->request_temp_pool,Receiver_Thread->store_area,&hadbigest);//换更大的暂存区
            //如果暂存区无法再扩大就断开连接
            if(hadbigest == 1) { FdClose(Receiver_Thread,fd,503); return 1; }
            RequestTempPoolWrite(fd_ob->request_temp_pool,fd);
        }
                
        // 发生了错误
        else if(state==0){

            fd_ob->ser_fina_send++;
            ReceiverToWorker(Receiver_Thread,fd,NULL,0,error_reason,serial);
            fd_ob->pack_in_path++;
            error_reason=200;
            serial++;
                    
            break;
        }
                      
       
   }

                
    return 1;

}



