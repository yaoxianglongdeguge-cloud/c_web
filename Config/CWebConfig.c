#include "../Include/Include.h"
#include <limits.h>

CWebConfig g_cfg;


#define CWEB_CFG_SET_MAX 64

static char g_set_keys[CWEB_CFG_SET_MAX][64];
static int  g_set_num;

static char* CfgTrim(char* s)
{
    while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++;

    char* e=s+strlen(s);
    while(e>s&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) e--;
    *e='\0';

    return s;
}

static int CfgParseInt(const char* s,int* out)
{
    if(s==NULL||*s=='\0') return 0;

    errno=0;
    char* end=NULL;
    long v=strtol(s,&end,10);

    if(end==s||errno==ERANGE) return 0;
    while(*end==' '||*end=='\t') end++;
    if(*end!='\0') return 0;
    if(v<(long)INT_MIN||v>(long)INT_MAX) return 0;

    *out=(int)v;
    return 1;
}

static int CfgSetStr(char* dst,size_t dstsz,const char* val)
{
    if(dst==NULL||dstsz==0||val==NULL) return 0;
    if(strlen(val)>=dstsz) return 0;

    snprintf(dst,dstsz,"%s",val);
    return 1;
}

static int CfgSetInt(int* dst,const char* val)
{
    if(dst==NULL) return 0;

    return CfgParseInt(val,dst);
}

/*
 * 配置注入：读到的键名和哪个字段对上，就直接写给哪个字段。
 * 返回 1 = 写入成功；0 = 值非法；-1 = 未知键（调用方只告警、不中断）
 */
static int CfgAssign(CWebConfig* c,const char* key,const char* val)
{
    if(c==NULL||key==NULL||val==NULL) return 0;

    /* ---- 监听 / 站点 ---- */
    if(strcmp(key,"listen.addr")==0)                 return CfgSetStr(c->listen_addr,sizeof(c->listen_addr),val);
    if(strcmp(key,"listen.port")==0)                 return CfgSetInt(&c->listen_port,val);
    if(strcmp(key,"listen.backlog")==0)              return CfgSetInt(&c->listen_backlog,val);
    if(strcmp(key,"web.root")==0)                    return CfgSetStr(c->web_root,sizeof(c->web_root),val);

    /* ---- 线程模型 ---- */
    if(strcmp(key,"group.num")==0)                   return CfgSetInt(&c->group_num,val);
    if(strcmp(key,"worker.per_group")==0)            return CfgSetInt(&c->worker_per_group,val);

    /* ---- 接待线程 ---- */
    if(strcmp(key,"receiver.idle_timeout_s")==0)     return CfgSetInt(&c->recv_idle_timeout_s,val);
    if(strcmp(key,"receiver.send_batch")==0)         return CfgSetInt(&c->recv_send_batch,val);
    if(strcmp(key,"timer.capacity")==0)              return CfgSetInt(&c->timer_capacity,val);
    if(strcmp(key,"fd_table.init_level")==0)         return CfgSetInt(&c->fd_table_init_level,val);

    /* ---- 队列 / 背压 ---- */
    if(strcmp(key,"task_queue.capacity")==0)         return CfgSetInt(&c->task_queue_capacity,val);
    if(strcmp(key,"temp_pack.capacity")==0)          return CfgSetInt(&c->temp_pack_capacity,val);
    if(strcmp(key,"send.pipeline_window")==0)        return CfgSetInt(&c->send_pipeline_window,val);

    /* ---- 内存池配额 ---- */
    if(strcmp(key,"pool.recv_http_kb")==0)           return CfgSetInt(&c->pool_recv_http_kb,val);
    if(strcmp(key,"pool.recv_store_kb")==0)          return CfgSetInt(&c->pool_recv_store_kb,val);
    if(strcmp(key,"pool.worker_txt_kb")==0)          return CfgSetInt(&c->pool_worker_txt_kb,val);
    if(strcmp(key,"pool.worker_http_kb")==0)         return CfgSetInt(&c->pool_worker_http_kb,val);

    /* ---- HTTP 限额 ---- */
    if(strcmp(key,"request.max_url_bytes")==0)       return CfgSetInt(&c->req_max_url,val);
    if(strcmp(key,"request.max_header_bytes")==0)    return CfgSetInt(&c->req_max_header,val);
    if(strcmp(key,"request.max_body_bytes")==0)      return CfgSetInt(&c->req_max_body,val);
    if(strcmp(key,"request.max_total_bytes")==0)     return CfgSetInt(&c->req_max_total,val);
    if(strcmp(key,"request.max_header_fields")==0)   return CfgSetInt(&c->req_max_header_fields,val);
    if(strcmp(key,"request.temp_init_kb")==0)        return CfgSetInt(&c->req_temp_init_kb,val);
    if(strcmp(key,"request.temp_pool_max_bytes")==0) return CfgSetInt(&c->req_temp_pool_max,val);

    /* ---- worker ---- */
    if(strcmp(key,"worker.stall_ms")==0)             return CfgSetInt(&c->worker_stall_ms,val);
    if(strcmp(key,"worker.sem_wait_ms")==0)          return CfgSetInt(&c->worker_sem_wait_ms,val);
    if(strcmp(key,"worker.idle_sleep_us")==0)        return CfgSetInt(&c->worker_idle_sleep_us,val);
    if(strcmp(key,"worker.parallel")==0)             return CfgSetInt(&c->worker_parallel,val);

    /* ---- 其它 ---- */
    if(strcmp(key,"epoll.tick_ms")==0)               return CfgSetInt(&c->epoll_tick_ms,val);

    return -1;
}

/* 记录“这个键被配置文件显式设置过”，供 CWebConfigIsSet 查询 */
static void CfgMarkSet(const char* key)
{
    if(key==NULL||*key=='\0') return;

    for(int i=0;i<g_set_num;i++){
        if(strcmp(g_set_keys[i],key)==0) return;   /* 已经有了，不重复记 */
    }

    if(g_set_num>=CWEB_CFG_SET_MAX) return;
    if(strlen(key)>=sizeof(g_set_keys[0])) return;

    snprintf(g_set_keys[g_set_num],sizeof(g_set_keys[0]),"%s",key);
    g_set_num++;
}

/* 本次 LoadFile 里这个键是否已经出现过（重复键告警用；from = 开读时的 g_set_num） */
static int CfgMarkedSince(const char* key,int from)
{
    for(int i=from;i<g_set_num;i++){
        if(strcmp(g_set_keys[i],key)==0) return 1;
    }
    return 0;
}

int CWebConfigSetDefaults(CWebConfig* c)
{
    if(c==NULL) return 0;

    snprintf(c->listen_addr,sizeof(c->listen_addr),"%s","127.0.0.1");
    c->listen_port=5343;
    c->listen_backlog=100;
    snprintf(c->web_root,sizeof(c->web_root),"%s","WebSite");

    c->group_num=CWEB_GROUP_NUM_DEFAULT;
    c->worker_per_group=CWEB_WORKER_PER_GROUP_DEFAULT;

    c->recv_idle_timeout_s=60;
    c->recv_send_batch=100;
    c->timer_capacity=5000;
    c->fd_table_init_level=4;

    c->task_queue_capacity=100;
    c->temp_pack_capacity=CWEB_TEMP_PACK_CAP;
    c->send_pipeline_window=100;

    c->pool_recv_http_kb=1024;
    c->pool_recv_store_kb=2048;
    c->pool_worker_txt_kb=2048;
    c->pool_worker_http_kb=256;

    c->req_max_url=8*1024;
    c->req_max_header=16*1024;
    c->req_max_body=1024*1024;
    c->req_max_total=c->req_max_body+64*1024;
    c->req_max_header_fields=CWEB_ANALYSISER_SPILL_MAX;
    c->req_temp_init_kb=2;
    c->req_temp_pool_max=1024*1024;

    c->worker_stall_ms=4;
    c->worker_sem_wait_ms=100;
    c->worker_idle_sleep_us=100000;
    c->worker_parallel=0;

    c->epoll_tick_ms=2;

    g_set_num=0;   /* 清掉上一轮的“已设置键”记录 */

    return 1;
}

int CWebConfigLoadFile(CWebConfig* c,const char* path)
{
    if(c==NULL||path==NULL) return 0;

    FILE* f=fopen(path,"r");
    if(f==NULL){
        fprintf(stderr,"[Config] 打开配置文件失败：%s\n",path);
        return 0;
    }

    char line[512];
    int  lineno=0;
    int  bad=0;
    int  marked_from=g_set_num;   /* 本次开读前已经记过的键数（重复键只在本次文件内判断） */

    while(fgets(line,sizeof(line),f)!=NULL){

        lineno++;

        size_t len=strlen(line);
        if(len>0&&line[len-1]!='\n'&&!feof(f)){
            fprintf(stderr,"[Config] %s:%d 行过长（>%d 字节），已忽略\n",path,lineno,(int)sizeof(line)-2);
            int ch=0;
            while((ch=fgetc(f))!='\n'&&ch!=EOF){}
            bad++;
            continue;
        }

        char* s=CfgTrim(line);

        if(*s=='\0'||*s=='#') continue;

        char* eq=strchr(s,'=');
        if(eq==NULL){
            fprintf(stderr,"[Config] %s:%d 缺少 '='，已忽略：%s\n",path,lineno,s);
            continue;
        }

        *eq='\0';

        char* key=CfgTrim(s);
        char* val=CfgTrim(eq+1);

        int r=CfgAssign(c,key,val);
        if(r<0){
            fprintf(stderr,"[Config] %s:%d 未知键 %s（已忽略）\n",path,lineno,key);
            continue;
        }
        if(r==0){
            fprintf(stderr,"[Config] %s:%d 键 %s 的值非法：%s\n",path,lineno,key,val);
            bad++;
            continue;
        }

        if(CfgMarkedSince(key,marked_from))
            fprintf(stderr,"[Config] %s:%d 键 %s 重复，后值覆盖前值\n",path,lineno,key);

        CfgMarkSet(key);
    }

    fclose(f);

    /* 一个已知键都没认出来：多半是第一行带 UTF-8 BOM，或键名拼错（CRLF 已自动处理） */
    if(g_set_num==marked_from&&bad==0)
        fprintf(stderr,"[Config] 提示：%s 里没有识别到任何已知键（检查是否带 UTF-8 BOM / 键名拼写）\n",path);

    return bad==0?1:0;
}

static int CfgFail(const char* key,const char* why)
{
    fprintf(stderr,"[Config] 非法配置 %s：%s\n",key,why);
    return 0;
}

int CWebConfigValidate(const CWebConfig* c)
{
    if(c==NULL) return 0;

    if(c->listen_addr[0]=='\0') return CfgFail("listen.addr","不能为空");
    if(c->listen_port<1||c->listen_port>65535) return CfgFail("listen.port","范围 1..65535");
    if(c->listen_backlog<1) return CfgFail("listen.backlog","必须 >=1");
    if(c->web_root[0]=='\0') return CfgFail("web.root","不能为空");

    if(c->group_num<1||c->group_num>CWEB_GROUP_NUM_MAX) return CfgFail("group.num","范围 1..32");
    if(c->worker_per_group<2||c->worker_per_group>CWEB_WORKER_PER_GROUP_MAX)
        return CfgFail("worker.per_group","范围 2..16（ThreadPoolInit 要求 >1）");

    if(c->recv_idle_timeout_s<1) return CfgFail("receiver.idle_timeout_s","必须 >=1");
    if(c->recv_send_batch<1) return CfgFail("receiver.send_batch","必须 >=1");
    if(c->timer_capacity<1) return CfgFail("timer.capacity","必须 >=1（且应 >= 单组最大连接数）");
    if(c->fd_table_init_level<1||c->fd_table_init_level>5) return CfgFail("fd_table.init_level","范围 1..5");

    if(c->task_queue_capacity<1) return CfgFail("task_queue.capacity","必须 >=1");
    if(c->temp_pack_capacity<1||c->temp_pack_capacity>CWEB_TEMP_PACK_CAP_MAX)
        return CfgFail("temp_pack.capacity","范围 1..65536");
    if(c->send_pipeline_window<1) return CfgFail("send.pipeline_window","必须 >=1");

    if(c->pool_recv_http_kb<64||c->pool_recv_http_kb>65536) return CfgFail("pool.recv_http_kb","范围 64..65536");
    if(c->pool_recv_store_kb<64||c->pool_recv_store_kb>65536) return CfgFail("pool.recv_store_kb","范围 64..65536");
    if(c->pool_worker_txt_kb<64||c->pool_worker_txt_kb>65536) return CfgFail("pool.worker_txt_kb","范围 64..65536");
    if(c->pool_worker_http_kb<64||c->pool_worker_http_kb>65536) return CfgFail("pool.worker_http_kb","范围 64..65536");

    if(c->req_max_url<256||c->req_max_url>65536) return CfgFail("request.max_url_bytes","范围 256..65536");
    if(c->req_max_header<1024||c->req_max_header>65536) return CfgFail("request.max_header_bytes","范围 1024..65536");
    if(c->req_max_body<1024) return CfgFail("request.max_body_bytes","必须 >=1024");
    if(c->req_max_total<c->req_max_header+c->req_max_body)
        return CfgFail("request.max_total_bytes","必须 >= max_header + max_body");
    if(c->req_max_header_fields<8) return CfgFail("request.max_header_fields","必须 >=8");
    if(c->req_temp_init_kb<1||c->req_temp_init_kb>64) return CfgFail("request.temp_init_kb","范围 1..64");
    if(c->req_temp_pool_max<1024) return CfgFail("request.temp_pool_max_bytes","必须 >=1024");

    if(c->worker_stall_ms<1||c->worker_stall_ms>1000) return CfgFail("worker.stall_ms","范围 1..1000");
    if(c->worker_sem_wait_ms<1||c->worker_sem_wait_ms>1000) return CfgFail("worker.sem_wait_ms","范围 1..1000");
    if(c->worker_idle_sleep_us<0||c->worker_idle_sleep_us>1000000)
        return CfgFail("worker.idle_sleep_us","范围 0..1000000");
    if(c->worker_parallel<0||c->worker_parallel>1) return CfgFail("worker.parallel","0 或 1");

    if(c->epoll_tick_ms<1||c->epoll_tick_ms>1000) return CfgFail("epoll.tick_ms","范围 1..1000");

    return 1;
}

void CWebConfigDump(const CWebConfig* c)
{
    if(c==NULL) return;

    printf("[Config] 生效配置：\n");
    printf("  listen.addr=%s  listen.port=%d  listen.backlog=%d  web.root=%s\n",
           c->listen_addr,c->listen_port,c->listen_backlog,c->web_root);
    printf("  group.num=%d  worker.per_group=%d\n",c->group_num,c->worker_per_group);
    printf("  receiver.idle_timeout_s=%d  receiver.send_batch=%d  timer.capacity=%d  fd_table.init_level=%d\n",
           c->recv_idle_timeout_s,c->recv_send_batch,c->timer_capacity,c->fd_table_init_level);
    printf("  task_queue.capacity=%d  temp_pack.capacity=%d  send.pipeline_window=%d\n",
           c->task_queue_capacity,c->temp_pack_capacity,c->send_pipeline_window);
    printf("  pool: recv_http=%dKB recv_store=%dKB worker_txt=%dKB worker_http=%dKB\n",
           c->pool_recv_http_kb,c->pool_recv_store_kb,c->pool_worker_txt_kb,c->pool_worker_http_kb);
    printf("  request: max_url=%d max_header=%d max_body=%d max_total=%d max_header_fields=%d temp_init=%dKB temp_pool_max=%d\n",
           c->req_max_url,c->req_max_header,c->req_max_body,c->req_max_total,
           c->req_max_header_fields,c->req_temp_init_kb,c->req_temp_pool_max);
    printf("  worker.stall_ms=%d  worker.sem_wait_ms=%d  worker.idle_sleep_us=%d  worker.parallel=%d  epoll.tick_ms=%d\n",
           c->worker_stall_ms,c->worker_sem_wait_ms,c->worker_idle_sleep_us,c->worker_parallel,c->epoll_tick_ms);
}

int CWebConfigIsSet(const char* key)
{
    if(key==NULL) return 0;

    for(int i=0;i<g_set_num;i++){
        if(strcmp(g_set_keys[i],key)==0) return 1;
    }
    return 0;
}

int CWebConfigInit(const char* path)
{
    if(CWebConfigSetDefaults(&g_cfg)!=1) return 0;

    const char* file=path;
    int explicit_path=0;

    if(file==NULL){
        file=getenv("CWEB_CONF");
        if(file!=NULL&&*file!='\0') explicit_path=1;
        else file=NULL;
    }else{
        explicit_path=1;
    }

    if(file==NULL) file=CWEB_CONF_DEFAULT_PATH;

    if(access(file,F_OK)==0){
        if(CWebConfigLoadFile(&g_cfg,file)!=1) return 0;
        printf("[Config] 已加载配置文件：%s\n",file);
    }else if(explicit_path){
        fprintf(stderr,"[Config] 配置文件不存在：%s\n",file);
        return 0;
    }else{
        printf("[Config] 未找到默认配置文件 %s，全部使用内置默认值\n",file);
    }
    fflush(stdout);

    /* 配置值只来自文件；这里不再读 CWEB_xxx 环境变量 */

    size_t n=strlen(g_cfg.web_root);
    while(n>1&&g_cfg.web_root[n-1]=='/') g_cfg.web_root[--n]='\0';

    return CWebConfigValidate(&g_cfg);
}

