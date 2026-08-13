
typedef struct timer_entry timer_entry;
typedef struct prior_queue_1{

    int end;//整个有效内存后面一位
    int elem_end;//最后一个元素后面一位
    timer_entry* queue;//0号为可以存储节点个数

}prior_queue_1;//小顶堆

