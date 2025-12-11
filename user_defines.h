#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#define MAX_NAME_LEN 64
#define MAX_REPOS 20
#define MAX_STAGING_FILES 100
#define MAX_REPO_PATH 512
#define MAX_MESSAGE_LEN 512
#define MAX_VERSIONS 127
#define MAX_LOCKS 32    
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
struct FileLock { 
    int lock_id; 
    char filename[256]; 
    char locked_by[64]; 
    time_t locked_at; 
    int lock_timeout; 
    int used; 
};
struct FileVersion {
    int version_id;
    char filename[MAX_NAME_LEN];
    char author[MAX_NAME_LEN];
    char message[MAX_MESSAGE_LEN];
    time_t timestamp;
    int parent_version_id;
    long size_bytes;
    int used;  // 1 if slot used, 0 if free
};
struct StagingFile {
    char filename[MAX_NAME_LEN];
    int used;
};
struct Repository  {
    char name[MAX_NAME_LEN];
    short unsigned int version;
    char repo_path[MAX_REPO_PATH];
    struct FileVersion versions[MAX_VERSIONS]; 
    unsigned file_count;
    unsigned number_of_commits;
    struct StagingFile staging[MAX_STAGING_FILES]; 
    time_t create_time;
    uint8_t active_locks;
    bool used; 
};
struct MainStruct{
    struct Repository repositories[MAX_REPOS];
    struct FileLock locks[MAX_LOCKS]; 
    int lock_count; 
    int next_lock_id; 
};
struct Config {
    int port;                   
    char logfile[256];      
    int max_workers;             
    int lock_timeout;                          
};

