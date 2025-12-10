 #include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "user_defines.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>
#include "helpers.h"
#include <fcntl.h>
struct MainStruct* mainstr;
int sem_id = -1;
int server_sock;
static struct Config config;
extern int errno;
static pid_t *workers = NULL;
static struct Config config;  
static int running = 1;
static int client_sock;//temporary

void worker_loop(int worker_id) {
    printf("INFO Worker %d (PID %d) started\n", worker_id, getpid());
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            if (running) perror("accept");
            sleep(1);
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("INFO Worker %d: client %s: %dF connected\n", worker_id, client_ip, ntohs(client_addr.sin_port));
        while (1) {
            int ret = process_command(client_sock, worker_id);
            if (ret < 0) {
                printf("INFO Worker %d: client disconnected\n", worker_id);
                break;
            }
        }
        close(client_sock);
        printf("INFO Worker %d: connection closed\n", worker_id);
    }
}
int process_command(int client_sock, int worker_id) {
    char buffer[2048];  
    int total_bytes = 0;
    
    // Читаем команду (может быть многострочная из-за файлов!)
    while (total_bytes < sizeof(buffer) - 1) {
        ssize_t bytes = recv(client_sock, buffer + total_bytes, sizeof(buffer) - total_bytes - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                // Клиент отключился
                return -1;
            }
            perror("recv");
            return -1;
        }
        
        total_bytes += bytes;
        // Ищем конец команды (новую строку)
        buffer[total_bytes] = '\0';
        if (strchr(buffer, '\n') != NULL) {
            break;
        }
    }
    
    if (total_bytes == 0) {
        return -1;  // Пустое соединение
    }
    
    // Убираем \n и парсим
    buffer[strcspn(buffer, "\n")] = '\0';
    
    printf("COMMAND Worker %d received: '%s'\n", worker_id, buffer);
    
    // Разбор команды
    char *cmd_name = strtok(buffer, " \t");
    if (!cmd_name) {
        send_response(client_sock, "ERROR: Empty command");
        return 0;  // Продолжаем ждать команды
    }
    
    // ДИСПЕТЧЕР КОМАНД
    int result = 0;
    if (strcmp(cmd_name, "INIT") == 0) {
        char* reponame = strtok(NULL, " ");
        result = cmd_init(client_sock, reponame);
    } 
    else if (strcmp(cmd_name, "ADD") == 0) {
        char* reponame = strtok(NULL, " ");
        char* filename = strtok(NULL, " ");
        result = cmd_add(client_sock, reponame, filename);
    }
    else if (strcmp(cmd_name, "LOG") == 0) {
        char* reponame = strtok(NULL, " ");
        result = cmd_log(client_sock, reponame);
    }  
    else if (strcmp(cmd_name, "COMMIT") == 0) {
        char *repoName = strtok(NULL, " ");
        char* message = strtok(NULL, "\"");  // Берем сообщение в кавычках
        if (message) {
            // Убираем открывающую кавычку, если есть
            if (message[0] == '"') message++;
            // Ищем закрывающую кавычку
            char *end_quote = strchr(message, '"');
            if (end_quote) *end_quote = '\0';
        }
        char* author = strtok(NULL, " ");
        result = cmd_commit(client_sock, repoName, message, author);
    }
    else if (strcmp(cmd_name, "LOCK") == 0) {
        char* reponame = strtok(NULL, " ");
        char* filename = strtok(NULL, " ");
        char* user = strtok(NULL, " ");
        result = cmd_lock(client_sock, reponame,filename,user);
    }
    else if (strcmp(cmd_name, "UNLOCK") == 0) {
        char* reponame = strtok(NULL, " ");
        char* filename = strtok(NULL, " ");
        char* user = strtok(NULL, " ");
        result = cmd_unlock(client_sock, reponame,filename,user);
    }
    else if (strcmp(cmd_name, "SHOW") == 0) {
        char* reponame = strtok(NULL, " ");
        int version_id=atoi(strtok(NULL, " "));
        result = cmd_show(client_sock, reponame,version_id);
    }    
    else {
        send_response(client_sock, "ERROR: Unknown command '%s'", cmd_name);
        printf("ERROR: Unknown command '%s'\n", cmd_name);
        return 0;
    }
    
    if (result == 0) {
        printf("SUCCESS Command '%s' completed by worker %d\n", cmd_name, worker_id);
    } else {
        printf("ERROR Command '%s' failed (worker %d)\n", cmd_name, worker_id);
    }
    
    return 0;  // Продолжаем обрабатывать команды
}

int main(int argc, char **argv){
    config=read_config(argv[1]);
    //логирование
    int logfd=open(config.logfile,O_WRONLY|O_CREAT,0666);
    if(logfd<0){
        perror("open");
    }
    //dup2(logfd,STDOUT_FILENO);
    //dup2(logfd,STDERR_FILENO);
    
    key_t key_sem = ftok("/tmp", 'A');
    sem_id = semget(key_sem, 1, 0666 | IPC_CREAT);

    union semun arg; 
    arg.val = 1;      
    semctl(sem_id, 0, SETVAL, arg);

    key_t key_mem = ftok("/",'S');
    int shmid = shmget(key_mem,sizeof(struct MainStruct),IPC_CREAT|0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    mainstr= shmat(shmid, NULL, 0);
    if (mainstr == (struct MainStruct *)(-1)) {
        perror("shmat");
        exit(1);
    }

    struct sockaddr_in addr;
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server_sock, 10) < 0) {
        perror("listen");
        return 1;
    }
    printf("serv sock main: %d\n",server_sock);
    workers = calloc(config.max_workers, sizeof(pid_t));
    if (!workers) {
        perror("calloc workers");
        return 1;
    }
    mkdir("/tmp/vcs_repos",0777);
    for (int i = 0; i < config.max_workers; i++) {
        spawn_worker(i);
        sleep(1);  // даём время на запуск
    }
    printf("INFO Server fully started with %d workers\n", config.max_workers);
    
    while (running) {
        sleep(5); 
        int alive_workers = 0;
        for (int i = 0; i < config.max_workers; i++) {
            if (workers[i] > 0) {
                if (kill(workers[i], 0) == 0) {
                    alive_workers++;
                } else {
                    printf("WARN Worker %d (PID %d) died, respawning", i, workers[i]);
                    spawn_worker(i);
                }
            }
        }
        
        // Удаляем истёкшие блокировки
        //cleanup_expired_locks();
        
        if (alive_workers == 0) {
            printf("ERROR No alive workers, restarting all\n");
            for (int i = 0; i < config.max_workers; i++) {
                spawn_worker(i);
            }
        }
    }

    printf("INFO Master process exiting\n");
    return 0;
}
void spawn_worker(int worker_id) {
    pid_t pid = fork();
    if (pid == 0) { //child-worker
        worker_loop(worker_id);
        exit(0);
    } else if (pid > 0) { //master
        workers[worker_id] = pid;
        printf("INFO Spawned worker %d (PID %d)\n", worker_id, pid);
    } else {
        perror("fork");
    }
}

int cmd_init(int client_sock,char* repoName) {
    if (!repoName || strlen(repoName) == 0) {
        send_response(client_sock, "No repo name");
        return -1;
    }
    
    int repo_idx = find_repo(repoName);
    sem_lock(MUTEX);
    if (repo_idx >= 0) {
        send_response(client_sock, "ERROR: Repository '%s' already exists", repoName); 
        sem_release(MUTEX);
        return -1;
    }
    int new_repo_idx = -1;
    for (int i = 0; i < MAX_REPOS; i++) {
        if (!mainstr->repositories[i].used) {
            new_repo_idx = i;
            mainstr->repositories[i]=create_new_repo(repoName);
            mainstr->repositories[i].used = true;
            break;
        }
        else mainstr->repositories[i].used = false;
    }
    sem_release(MUTEX);
    
    if (new_repo_idx < 0) {
        send_response(client_sock, "ERROR: Maximum repositories reached (%d)", MAX_REPOS);
        return -1;
    }
    char path[MAX_REPO_PATH];
    snprintf(path,sizeof(path),"/tmp/vcs_repos/%s",repoName);
    mkdir(path,0777);
    send_response(client_sock, "OK: Repository '%s' created (ID: %d)", repoName, new_repo_idx);
    return 0;
    
}

int cmd_add(int client_sock,char *repoName, char *filename) {
    if (!repoName || !filename) {
        send_response(client_sock, "ERROR: Missing repo_name or filename");
        return -1;
    }
    
    // 1. НАХОДИМ репозиторий
    int repo_idx = find_repo(repoName);
    sem_lock(MUTEX);
    if (repo_idx < 0) {
        send_response(client_sock, "ERROR: Repository '%s' not found", repoName);
        sem_release(MUTEX);
        return -1;
    }
    // 2. ПРОВЕРЯЕМ staging area — файл уже есть?
    for (int j = 0; j < MAX_STAGING_FILES; j++) {
        if (mainstr->repositories[repo_idx].staging[j].used && strcmp(mainstr->repositories[repo_idx].staging[j].filename, filename) == 0) {
            sem_release(MUTEX);
            send_response(client_sock, "ERROR: File '%s' already in staging", filename);
            return -1;
        }
    }
    
    int staging_idx = -1;
    for (int j = 0; j < MAX_STAGING_FILES; j++) {
        if (!mainstr->repositories[repo_idx].staging[j].used) {
            staging_idx = j;
            mainstr->repositories[repo_idx].staging[j].used = true;
            strcpy(mainstr->repositories[repo_idx].staging[j].filename,filename);
            mainstr->repositories[repo_idx].file_count++;
            break;
        }
    }
    if (staging_idx < 0) {
        sem_release(MUTEX);
        send_response(client_sock, "ERROR: Staging area full");
        return -1;
    }
    sem_release(MUTEX);
    send_response(client_sock, "OK: '%s' registered in staging (slot %d)", filename, staging_idx);
    return 0;
}
int cmd_commit(int client_sock,char *repoName, char* message, char* author) {
    if (!repoName || !message || !author) {
        send_response(client_sock, "ERROR: Missing repo_name, message or author");
        return -1;
    }
    int repo_idx = find_repo(repoName);
    if (repo_idx < 0) {
        send_response(client_sock, "ERROR: Repository '%s' not found", repoName);
        return -1;
    }
    sem_lock(MUTEX);
    
    if (mainstr->repositories[repo_idx].file_count == 0) {
        send_response(client_sock, "ERROR: No files in staging area");
        sem_release(MUTEX);
        return -1;
    }

    if (mainstr->repositories[repo_idx].active_locks > 0) {
        sem_release(MUTEX);
        send_response(client_sock, "ERROR: Some files are locked");
        return -1;
    }
    // создаём директорию
    int local_version_id = mainstr->repositories[repo_idx].version++;
    char version_dir[MAX_REPO_PATH+10]; //для выравнивания
    snprintf(version_dir, sizeof(version_dir), "%s/%s/v%d/",mainstr->repositories[repo_idx].repo_path, repoName, local_version_id);
    mkdir(version_dir, 0755);
    
    // отправляем старт коммит
    send_response(client_sock, "COMMIT_START: %d files, version %d",mainstr->repositories[repo_idx].file_count, local_version_id);
    
    // подгружаем  из staging
    long total_size = 0;
    int files_committed = 0;
    
    for (int j = 0; j < MAX_STAGING_FILES; j++) {
        if (mainstr->repositories[repo_idx].staging[j].used) {
            char filename[MAX_NAME_LEN];
            strncpy(filename, mainstr->repositories[repo_idx].staging[j].filename, MAX_NAME_LEN);
            
            // запрашиваем файл
            char request[512];
            snprintf(request, sizeof(request), "NEED_FILE %s\n", filename);
            send(client_sock, request, strlen(request), 0);
            
            // Читаем размер
            char size_buf[32];
            recv(client_sock, size_buf, sizeof(size_buf) - 1, 0);
            size_buf[strcspn(size_buf, "\n")] = 0;
            long file_size = atol(size_buf);
            
            // сохраняем файл 
            char file_path[MAX_REPO_PATH+MAX_NAME_LEN+10]; //для выравнивания
            snprintf(file_path, sizeof(file_path), "%s/%s", version_dir, filename);
            FILE *f = fopen(file_path, "wb"); //BINARY
            
            if (f) {
                char buf[4096];
                size_t received = 0;
                while (received < file_size) {
                    ssize_t bytes = recv(client_sock, buf, sizeof(buf), 0);
                    if (bytes <= 0) break;
                    fwrite(buf, 1, bytes, f);
                    received += bytes;
                }
                fclose(f);
                total_size += received;
                files_committed++;
                
                send_response(client_sock, "FILE_OK: %s (%ld bytes)", filename, received);
            } else {
                send_response(client_sock, "FILE_ERROR: %s", filename);
            }
        }
    }
    
    int version_idx = -1;
    for (int i = 0; i < MAX_VERSIONS; i++) {
        if (!mainstr->repositories[repo_idx].versions[i].used) {
            version_idx = i;
            mainstr->repositories[repo_idx].versions[version_idx].version_id = local_version_id;
            strncpy(mainstr->repositories[repo_idx].versions[version_idx].filename, "commit_bundle", MAX_NAME_LEN-1);
            strncpy(mainstr->repositories[repo_idx].versions[version_idx].author, author, MAX_NAME_LEN-1);
            strncpy(mainstr->repositories[repo_idx].versions[version_idx].message, message, MAX_MESSAGE_LEN-1);
            mainstr->repositories[repo_idx].versions[version_idx].timestamp = time(NULL);
            mainstr->repositories[repo_idx].versions[version_idx].parent_version_id = local_version_id-1; 
            mainstr->repositories[repo_idx].versions[version_idx].size_bytes = total_size;
            mainstr->repositories[repo_idx].versions[version_idx].used = 1;
            break;
        }
    }
    
    //очищаем staging и обновляем статистику
    for (int j = 0; j < MAX_STAGING_FILES; j++) {
        mainstr->repositories[repo_idx].staging[j].used = 0;
    }
    mainstr->repositories[repo_idx].file_count = 0;
    mainstr->repositories[repo_idx].number_of_commits++;
    
    sem_release(MUTEX);    
    send_response(client_sock, "COMMIT_OK: Version %d created (%d files, %ld bytes)", local_version_id, files_committed, total_size);
    return 0;
}
int cmd_log(int client_sock, char * repoName){
    if (!repoName) {
        send_response(client_sock, "ERROR: Missing repo name");
        return -1;
    }
    int repo_idx = find_repo(repoName);
    if (repo_idx < 0) {
        send_response(client_sock, "ERROR: Repository '%s' not found", repoName);
        return -1;
    }
    sem_lock(MUTEX);
    int commit_count = mainstr->repositories[repo_idx].number_of_commits;
    char resp[commit_count*(MAX_MESSAGE_LEN+MAX_NAME_LEN+1)];
    resp[0] = '\0'; 
    for(int i = 0;i<commit_count;i++){
        char string[MAX_MESSAGE_LEN+MAX_NAME_LEN+1+50];//для выравнивания областей
        sprintf(string,"Commit number %d. Author:%s. Commit message:%s\n",i,mainstr->repositories[repo_idx].versions[i].author,mainstr->repositories[repo_idx].versions[i].message);
        strcat(resp,string);
    }
    sem_release(MUTEX);
    send_response(client_sock,resp);
    return 0;
}
int cmd_lock(int client_sock, char *repoName, char *filename, char *user) {
  
    if (!repoName || !filename || !user) {
        send_response(client_sock, "ERROR: Missing repo_name, filename or user");
        return -1;
    }

    int repo_idx = find_repo(repoName);
    if (repo_idx < 0) {
        send_response(client_sock, "ERROR: Repository '%s' not found", repoName);
        return -1;
    }

    sem_lock(SEM_LOCK_MANAGER);

    int existing_idx = -1;
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (mainstr->locks[i].used && strcmp(mainstr->locks[i].filename, filename) == 0) {
            existing_idx = i;
            break;
        }
    }

    if (existing_idx >= 0) {
        const char *owner = mainstr->locks[existing_idx].locked_by; //уже заблочен
        sem_release(SEM_LOCK_MANAGER);
        send_response(client_sock, "ERROR: File '%s' already locked by '%s'", filename, owner);
        return -1;
    }
    int free_idx = -1;
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (!mainstr->locks[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        sem_release(SEM_LOCK_MANAGER);;
        send_response(client_sock, "ERROR: No free lock slots");
        return -1;
    }
    int lock_id = mainstr->next_lock_id++;
    mainstr->locks[free_idx].lock_id = lock_id;
    strncpy(mainstr->locks[free_idx].filename,filename,sizeof(mainstr->locks[free_idx].filename) - 1);
    strncpy(mainstr->locks[free_idx].locked_by, user,sizeof(mainstr->locks[free_idx].locked_by) - 1);
    mainstr->locks[free_idx].locked_at = time(NULL);
    mainstr->locks[free_idx].lock_timeout= config.lock_timeout;
    mainstr->locks[free_idx].used = 1;
    mainstr->lock_count++;

    sem_release(SEM_LOCK_MANAGER);

    sem_lock(MUTEX);
    mainstr->repositories[repo_idx].active_locks++;
    sem_release(MUTEX);

    send_response(client_sock, "OK: File '%s' locked by '%s' (lock_id=%d, timeout=%d)",filename, user, lock_id, config.lock_timeout);
    return 0;
}
int cmd_unlock(int client_sock, char *repoName, char *filename, char *user) {
    if (!repoName || !filename || !user) {
        send_response(client_sock, "ERROR: Missing repo_name, filename or user");
        return -1;
    }
    int repo_idx = find_repo(repoName);

    if (repo_idx < 0) {
        send_response(client_sock, "ERROR: Repository '%s' not found", repoName);
        return -1;
    }

    sem_lock(SEM_LOCK_MANAGER);

    int lock_idx = -1;
    for (int i = 0; i < MAX_LOCKS; i++) {
        if (mainstr->locks[i].used && strcmp(mainstr->locks[i].filename, filename) == 0) {
            lock_idx = i;
            break;
        }
    }

    if (lock_idx < 0) {
        sem_release(SEM_LOCK_MANAGER);
        send_response(client_sock, "ERROR: File '%s' is not locked", filename);
        return -1;
    }

    if (strcmp(mainstr->locks[lock_idx].locked_by, user) != 0) {
        const char *owner = mainstr->locks[lock_idx].locked_by;
        sem_release(SEM_LOCK_MANAGER);
        send_response(client_sock,"ERROR: Lock on '%s' belongs to '%s', not '%s'",filename, owner, user);
        return -1;
    }

    memset(&mainstr->locks[lock_idx], 0, sizeof(struct FileLock));
    mainstr->lock_count--;
    sem_release(SEM_LOCK_MANAGER);

    sem_lock(MUTEX);
    if (mainstr->repositories[repo_idx].active_locks > 0)
        mainstr->repositories[repo_idx].active_locks--;
    sem_release(MUTEX);

    send_response(client_sock, "OK: File '%s' unlocked by '%s'", filename, user);
    return 0;
}
int cmd_show(int client_sock, char *repoName,int version_id) {
    
    if (!repoName || version_id<0) {
        send_response(client_sock, "ERROR: Missing repo_name or wrong version_id");
        return -1;
    }
    int repo_idx = find_repo(repoName);
    if (repo_idx<0) {
        send_response(client_sock, "ERROR: Missing repository not found");
        return -1;
    }
    // 1. НАХОДИМ версию в массиве versions[]
    sem_lock(MUTEX);
    struct FileVersion *found_version = NULL;
    for (int i = 0; i < MAX_VERSIONS; i++) {
        if (mainstr->repositories[repo_idx].versions[i].used && mainstr->repositories[repo_idx].versions[i].version_id == version_id ) {  
            found_version = &mainstr->repositories[repo_idx].versions[i];
            break;
        }
    }
    sem_release(MUTEX);
    
    if (!found_version) {
        send_response(client_sock, "ERROR: Version %d not found in repo '%s'", version_id, repoName);
        return -1;
    }
    char response[MAX_MESSAGE_LEN+MAX_NAME_LEN+68];
    snprintf(response, sizeof(response),
        "OK: Version #%d in '%s'\n"
        " Author: %s\n"
        " Message: %s\n"
        " Timestamp: %s\n"
        //"  Total size: %d bytes\n"
        " Parent: %d\n",
        //"  Files: %s",
        version_id, 
        repoName,
        found_version->author,
        found_version->message,
        ctime(&found_version->timestamp),
        //total_size,
        found_version->parent_version_id
        //files_list[0] ? files_list : "none");
);
    send_response(client_sock,response);
    
    return 0;
}