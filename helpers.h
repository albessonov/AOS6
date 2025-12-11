#define MUTEX 0
#define SEM_LOCK_MANAGER 1
int sem_lock(int sem_num);
int sem_release(int sem_num);
int find_repo(const char *name);
void send_response(int client_sock, const char *format, ...) ;
struct Repository create_new_repo(char* repoName);
unsigned int hash_func(const char *str) ;
int process_command(int client_sock, int worker_id);
void spawn_worker(int worker_id);
struct Config read_config(const char *filename);
void cleanup_expired_locks(void);