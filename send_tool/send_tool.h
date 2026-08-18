typedef struct Send_tool_Entry{

    Send_pack* send_pack;
    int use;

}Send_tool_Entry;

 //由于我们假设发回包的速度比接收返回包更快，
//所以指针队列不会满，所以说那个下一个要发挥哪一个不需要特别一个变量注明，只需要根据此时指向的数组位置有没有要发的就可以

int send_tool_init(Send_tool** s,int blocknum);

int send_tool_insert(Send_tool* s,int serial,char* ch,int error_reason);

int send_tool_delete(Send_tool* s,int serial);