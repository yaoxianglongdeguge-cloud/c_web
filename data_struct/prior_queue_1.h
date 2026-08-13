
typedef struct timer_entry 
{
    int fd;
    time_t time;

}timer_entry;

typedef struct prior_queue_1{

    int end;//整个有效内存后面一位
    int elem_end;//最后一个元素后面一位
    timer_entry* queue;//0号为可以存储节点个数

}prior_queue_1;//小顶堆


int prior_queue_1_init(prior_queue_1** p,int num);

int prior_queue_1_insert(prior_queue_1* p,int fd,time_t time);

int prior_queue_1_pop(prior_queue_1* p);

timer_entry prior_queue_1_top(prior_queue_1* p);

int prior_queue_1_down(prior_queue_1* p,int t);
