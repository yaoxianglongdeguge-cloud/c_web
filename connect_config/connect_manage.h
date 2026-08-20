typedef struct worker worker; 
typedef struct Hash_Entry_3 Fd_Entry;
typedef struct Hash_map_3 Fd_Table;

int fd_connect(worker* w,int Listen_fd);

int fd_close(worker* w,int fd);