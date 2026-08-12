
typedef struct send_tool Send_tool; //由于我们假设发回包的速度比接收返回包更快，
//所以指针队列不会满，所以说那个下一个要发挥哪一个不需要特别一个变量注明，只需要根据此时指向的数组位置有没有要发的就可以

typedef struct send_queue Send_queue;