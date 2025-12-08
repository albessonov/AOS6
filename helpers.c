#include "helpers.h"
#include "user_defines.h"
#include <sys/sem.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
extern int sem_id;
extern struct MainStruct *mainstr; 
//забираем семафор
int sem_lock(int sem_num) {
    struct sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}
// высвобождаем семафор
int sem_release(int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}
// находим репозиторий по имени
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
