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
//забираем семафор
int sem_lock(int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
// высвобождаем семафор
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
        // Используем значения по умолчанию
        config.port = 9999;
        config.max_workers = 4;
        config.lock_timeout = 300;
        strncpy(config.logfile, "vcs-server", sizeof(config.logfile));
        config.shm_file = "/tmp";
        config.sem_file = "/tmp";
        return config;  // Не фатальная ошибка
    }
    
    char line[256];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        line[strcspn(line, "\n")] = 0;  // Убираем \n
        
        // Пропускаем комментарии и пустые строки
        if (line[0] == '#' || line[0] == '\0') continue;
        
        // Парсим параметры
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
            if (sscanf(line + 12, "%d", &config.lock_timeout) != 1 || config.lock_timeout < 30) {
                fprintf(stderr, "Config error line %d: invalid lock_timeout\n", line_num);
                config.lock_timeout = 300;
            }
        }
        else if (strncmp(line, "shm_file", 7) == 0) {
            sscanf(line + 7, "%s", &config.shm_file);
        }
        else if (strncmp(line, "sem_file", 7) == 0) {
            sscanf(line + 7, "%s", &config.sem_file);
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
//создаем новый репозиторий
struct Repository create_new_repo(char* repoName) {
    struct Repository new_repo;
    new_repo.create_time=time(NULL);
    new_repo.file_count=0;
    strncpy(new_repo.name,repoName,strlen(repoName));
    new_repo.name[strlen(repoName)] = '\0';
    printf("new rep name:%s\n",new_repo.name);
    new_repo.number_of_commits=0;
    new_repo.version=0;
    strncpy(new_repo.repo_path,"/tmp/vcs_repos",15);
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
unsigned int hash_func(const char *str) {
    unsigned int hash = 0;
    while (*str) {
        hash = (hash * 31) + *str;
        str++;
    }
    return hash;
}
