typedef struct Send_tool_early{

    prior_queue_1* p;

}Send_tool_early;

int send_tool_early_init(Send_tool_early** s,int num);

int send_tool_early_insert(Send_tool_early* s,int fd_site);

int send_tool_early_top(Send_tool_early* s);

int send_tool_early_pop(Send_tool_early* s,int fd_site);