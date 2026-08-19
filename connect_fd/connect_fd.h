typedef struct Hash_Entry_3 Fd_Entry; 
typedef struct Hash_map_3 Fd_Table;


int Fd_Table_init(Fd_Table** f,int init);

int Fd_Table_insert(Fd_Table* f,int url);

int Fd_Table_delete(Fd_Table* f,int url);

int Fd_Table_find(Fd_Table* f,int url,Fd_Entry** ptr);

