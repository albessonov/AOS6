#include "helpers.h"
#include "user_defines.h"
#include <sys/sem.h>
#include <sys/socket.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
extern int sem_id;
extern struct MainStruct *mainstr; 
void cleanup_expired_locks(void) {
    time_t now = time(NULL);

    sem_lock(SEM_LOCK_MANAGER);

    for (int i = 0; i < MAX_LOCKS; i++) {
        if (!mainstr->locks[i].used) continue;

        time_t locked_at = mainstr->locks[i].locked_at;
        int timeout_sec = mainstr->locks[i].lock_timeout;

        if (now - locked_at >= timeout_sec) {
    
            char expired_file[MAX_REPO_PATH];
            strncpy(expired_file, mainstr->locks[i].filename, sizeof(expired_file) - 1);
            expired_file[sizeof(expired_file) - 1] = '\0';

            printf("INFO Expired lock %d on '%s' by '%s' (timeout %d s) removed\n",mainstr->locks[i].lock_id, mainstr->locks[i].filename,mainstr->locks[i].locked_by,timeout_sec);

            mainstr->locks[i].used = 0;
            mainstr->lock_count--;

            sem_release(SEM_LOCK_MANAGER);
            sem_lock(MUTEX);

            for (int r = 0; r < MAX_REPOS; r++) {
                if (!mainstr->repositories[r].used)
                    continue;
                if (mainstr->repositories[r].active_locks > 0) {
                    mainstr->repositories[r].active_locks--;
                    break;
                }
            }

            sem_release(MUTEX);
            sem_lock(SEM_LOCK_MANAGER);
        }
    }

    sem_release(SEM_LOCK_MANAGER);
}
int sem_lock(int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
int sem_release(int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}
//читаем и парсим конфиг
struct Config read_config(const char *filename) {
    struct Config config;
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen config");
        config.port = 9999;
        config.max_workers = 4;
        config.lock_timeout = 300;
        strncpy(config.logfile, "vcs-server", sizeof(config.logfile));
        return config;  
    }
    
    char line[256];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        line[strcspn(line, "\n")] = 0;  
        
        if (line[0] == '#' || line[0] == '\0') continue; //комм
        
        if (strncmp(line, "port", 4) == 0) {
            if (sscanf(line + 4, "%d", &config.port) != 1 || config.port <= 0 || config.port > 65535) {
                fprintf(stderr, "Config error line %d: invalid port\n", line_num);
                config.port = 9999;
            }
        }
        else if (strncmp(line, "logfile", 7) == 0) {
            sscanf(line + 7, "%s", config.logfile);
        }
        else if (strncmp(line, "max_workers", 11) == 0) {
            if (sscanf(line + 11, "%d", &config.max_workers) != 1 || config.max_workers <= 0 || config.max_workers > 32) {
                fprintf(stderr, "Config error line %d: invalid max_workers\n", line_num);
                config.max_workers = 4;
            }
        }
        else if (strncmp(line, "lock_timeout", 12) == 0) {
            if (sscanf(line + 12, "%d", &config.lock_timeout) != 1 ) {
                fprintf(stderr, "Config error line %d: invalid lock_timeout\n", line_num);
                config.lock_timeout = 111;
            }
        }
        else {
            fprintf(stderr, "Config warning line %d: unknown parameter '%s'\n", line_num, line);
        }
    }
    fclose(f);
    
    printf("INFO Config loaded: port=%d workers=%d timeout=%ds\n", config.port, config.max_workers, config.lock_timeout);
    return config;
}
int find_repo(const char *name) {
    sem_lock(MUTEX);
    for (int i = 0; i < MAX_REPOS; i++) {
        if (mainstr->repositories[i].used && strcmp(mainstr->repositories[i].name, name) == 0) {
            sem_release(MUTEX);
            return i;
        }
    }
    sem_release(MUTEX);
    return -1;
}
struct Repository create_new_repo(char* repoName) {
    struct Repository new_repo;
    memset(&new_repo, 0, sizeof(new_repo));  
    new_repo.create_time=time(NULL);
    new_repo.file_count=0;
    strncpy(new_repo.name,repoName,strlen(repoName));
    new_repo.name[strlen(repoName)] = '\0';
    printf("new rep name:%s\n",new_repo.name);
    new_repo.number_of_commits=0;
    new_repo.version=0;
    strncpy(new_repo.repo_path,"/tmp/vcs_repos",MAX_REPO_PATH - 1);
    new_repo.repo_path[MAX_REPO_PATH - 1] = '\0';
    return new_repo;
}
void send_response(int client_sock, const char *format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    strcat(buffer, "\n");
    printf("%s", buffer);
    send(client_sock, buffer, strlen(buffer), 0);
}

