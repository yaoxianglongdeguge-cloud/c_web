#ifndef CWEB_CONFIG_H
#define CWEB_CONFIG_H

/*
 * 运行期配置：一个结构体 + 一个“键名 -> 字段”的 if 注入函数。
 *   加一个配置项 =  ① 这里加字段
 *                   ② Config/CWebConfig.c 的 CfgAssign() 加一行 if
 *   配置值只来自配置文件，不读 CWEB_xxx 环境变量（CWEB_CONF 只用于指定配置文件路径）。
 */

#define CWEB_GROUP_NUM_DEFAULT         1
#define CWEB_GROUP_NUM_MAX             32
#define CWEB_WORKER_PER_GROUP_DEFAULT  4
#define CWEB_WORKER_PER_GROUP_MAX      16

typedef struct CWebConfig{

    char listen_addr[64];
    int  listen_port;
    int  listen_backlog;
    char web_root[256];

    int  group_num;
    int  worker_per_group;

    int  recv_idle_timeout_s;
    int  recv_send_batch;
    int  timer_capacity;
    int  fd_table_init_level;

    int  task_queue_capacity;
    int  temp_pack_capacity;
    int  send_pipeline_window;

    int  pool_recv_http_kb;
    int  pool_recv_store_kb;
    int  pool_worker_txt_kb;
    int  pool_worker_http_kb;

    int  req_max_url;
    int  req_max_header;
    int  req_max_body;
    int  req_max_total;
    int  req_max_header_fields;
    int  req_temp_init_kb;
    int  req_temp_pool_max;

    int  worker_stall_ms;
    int  worker_sem_wait_ms;
    int  worker_idle_sleep_us;
    int  worker_parallel;

    int  epoll_tick_ms;

}CWebConfig;

extern CWebConfig g_cfg;

#define CWEB_CONF_DEFAULT_PATH  "/etc/cweb.conf"

int  CWebConfigSetDefaults(CWebConfig* c);

//key = value，整行 # 注释；返回 0 表示文件打不开或有值非法（未知键只告警）
int  CWebConfigLoadFile(CWebConfig* c,const char* path);

int  CWebConfigValidate(const CWebConfig* c);

void CWebConfigDump(const CWebConfig* c);

int  CWebConfigInit(const char* path);

//该键是否被配置文件显式设置过（key 用文件里的写法，如 "listen.port"）
int  CWebConfigIsSet(const char* key);

#endif
