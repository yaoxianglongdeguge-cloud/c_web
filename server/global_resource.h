typedef struct Task_queue Task_queue;
extern Task_queue* Task_Queue;

typedef const struct Http_analysis_1* const Request;

typedef struct Response{} Response;

extern sem_t sem_task_queue_notfull;

extern pthread_mutex_t mutex_task;

extern sem_t sem_task_queue_notempty;


int global_resource_init(int Task_Queue_size);
