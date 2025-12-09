
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#define BUFFER_SIZE 4096

int connect_to_server(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    struct hostent *he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "Unknown host %s\n", host);
        close(sock);
        return -1;
    }
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    
    return sock;
}

void send_command(int sock, const char *format, ...) {
    char cmd[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(cmd, sizeof(cmd), format, args);
    va_end(args);
    strcat(cmd, "\n");
    printf("SENDING: %s\n",cmd);
    send(sock, cmd, strlen(cmd), 0);
}

void receive_response(int sock) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
}
void cmd_init(const char *host, int port, const char *repo_name) {
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    
    send_command(sock, "INIT %s", repo_name);
    receive_response(sock);
    close(sock);
}
// ==================== ADD (только регистрация) ====================
void cmd_add(const char *host, int port, const char *repo, const char *filename) {
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ADD %s %s", repo, filename);
    send_command(sock, "%s", cmd);
    receive_response(sock);
    close(sock);
}

void cmd_commit_files(const char *host, int port, const char *repo,  const char *message, const char *author, char *filenames[], int num_files) {
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    
    printf("=== Starting commit for repo '%s' ===\n", repo);
    
    for(int i = 0; i < num_files; i++) {
        struct stat check;
        if (stat(filenames[i], &check) != 0) {
            fprintf(stderr, "File %s not found\n", filenames[i]);
            close(sock);
            exit(1);
        }
    }
    
    //add
    for (int i = 0; i < num_files; i++) {
        printf("Registering %s...\n", filenames[i]);
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "ADD %s %s", repo, filenames[i]);
        send_command(sock, "%s", cmd);
        
        char response[BUFFER_SIZE];
        recv(sock, response, sizeof(response) - 1, 0);
        response[strcspn(response, "\n")] = '\0';
        printf("Server response on add: %s\n", response);
        
        if (strncmp(response, "OK:", 3) != 0 && strstr(response,"already in staging")==NULL) {
            fprintf(stderr, "Failed to add file %s: %s\n", filenames[i], response);
            close(sock);
            exit(1);
        }
    }
    
    // 2. Отправляем COMMIT
    printf("Sending COMMIT command...\n");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "COMMIT %s \"%s\" %s", repo, message, author);
    send_command(sock, "%s", cmd);
    
    // 3. Получаем COMMIT_START
    char buffer[BUFFER_SIZE];
    int res = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if(res<0){
        printf("recv() falied %s\n",strerror(errno));
    }
    buffer[strcspn(buffer, "\n")] = '\0';
    printf("Server: %s\n", buffer);
    if(strstr(buffer,"ERROR")!=NULL){
        close(sock);
        return;
    }
    if (strncmp(buffer, "COMMIT_START:", 13) != 0) {
        recv(sock, buffer, sizeof(buffer) - 1, 0);
        buffer[strcspn(buffer, "\n")] = '\0';
        printf("Server: %s\n", buffer);
        fprintf(stderr, "Commit failed to start: %s\n", buffer);
        close(sock);
        return;
    }
    
    // 4. Отправляем каждый файл по запросу сервера
    int files_to_send = num_files;
    while (files_to_send > 0) {
        // Ждём NEED_FILE filename
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0';
        
        if (strncmp(buffer, "NEED_FILE", 9) == 0) {
            char needed_file[256];
            sscanf(buffer, "NEED_FILE %s", needed_file);
            
            // Ищем запрошенный файл в нашем списке
            int file_found = 0;
            for (int i = 0; i < num_files; i++) {
                if (strcmp(needed_file, filenames[i]) == 0) {
                    printf("Sending %s...\n", filenames[i]);
                    file_found = 1;
                    
                    // Отправляем размер
                    struct stat st;
                    if (stat(filenames[i], &st) == 0) {
                        char size_str[32];
                        snprintf(size_str, sizeof(size_str), "%ld\n", (long)st.st_size);
                        send(sock, size_str, strlen(size_str), 0);
                        
                        // Отправляем содержимое
                        int fd = open(filenames[i], O_RDONLY);
                        if (fd >= 0) {
                            char buf[4096];
                            ssize_t bytes_read;
                            while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
                                send(sock, buf, bytes_read, 0);
                            }
                            close(fd);
                        }
                    }
                    files_to_send--;
                    break;
                }
            }
            
            if (!file_found) {
                fprintf(stderr, "Requested file not found: %s\n", needed_file);
                // Отправляем 0 размер, чтобы сервер пропустил этот файл
                send(sock, "0\n", 2, 0);
            }
            
            // Получаем подтверждение
            recv(sock, buffer, sizeof(buffer) - 1, 0);
            buffer[strcspn(buffer, "\n")] = '\0';
            printf("Server: %s\n", buffer);
        }
        else if (strncmp(buffer, "COMMIT_OK", 9) == 0 || 
                 strncmp(buffer, "ERROR", 5) == 0) {
            // Сервер завершил commit
            printf("Server: %s\n", buffer);
            break;
        }
    }
    
    // 5. Финальный результат
    recv(sock, buffer, sizeof(buffer) - 1, 0);
    buffer[strcspn(buffer, "\n")] = '\0';
    printf("=== COMMIT RESULT ===\n");
    printf("Server: %s\n", buffer);
    
    close(sock);
}
void cmd_log(const char *host, int port,char *repoName){
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "LOG %s", repoName);
    send_command(sock, "%s", cmd);
    receive_response(sock);
    close(sock);  
} 
// ==================== MAIN ====================
int main(int argc, char *argv[]) {
        if (argc < 2) {
        printf("Usage:\n");
        printf("  %s init localhost 9999 myrepo\n", argv[0]);
        printf("  %s add localhost 9999 myrepo <file>\n", argv[0]);
        printf("  %s lock localhost 9999 myrepo <file> <user>\n", argv[0]);
        printf("  %s unlock localhost 9999 myrepo <file> <user>\n", argv[0]);
        printf("  %s commit localhost 9999 myrepo \"msg\" <user> <file1> [file2...]\n", argv[0]);
        printf("  %s log localhost 9999 myrepo\n", argv[0]);
        printf("  %s show localhost 9999 myrepo <version_id>\n", argv[0]);
        return 1;
    }
    
    char *host = argv[2];
    int port = atoi(argv[3]);
    
    if (strcmp(argv[1], "init") == 0 && argc == 5) {
        cmd_init(host, port, argv[4]);
    }
    else if (strcmp(argv[1], "add") == 0 && argc == 6) {
        cmd_add(host, port, argv[4], argv[5]);
    }
    else if (strcmp(argv[1], "lock") == 0 && argc == 7) {
        cmd_lock(host, port, argv[4], argv[5], argv[6]);
    }
    else if (strcmp(argv[1], "unlock") == 0 && argc == 7) {
        cmd_unlock(host, port, argv[4], argv[5], argv[6]);
    }
    else if (strcmp(argv[1], "commit") == 0 && argc >= 8) {
        char *repo = argv[4];
        char *message = argv[5];
        char *author = argv[7];
        int num_files = argc - 7;
        char *filenames[32];
        
        for (int i = 0; i < num_files && i < 32; i++) {
            filenames[i] = argv[7 + i];
        }
        
        cmd_commit_files(host, port, repo, message, author, filenames, num_files);
    }
    else if (strcmp(argv[1], "log") == 0 && argc == 5) {
        cmd_log(host, port, argv[4]);
    }
    else {
        printf("Invalid command or arguments.\n");
    }
    return 0;
}
void cmd_lock(const char *host, int port, const char *repo, const char *filename, const char *user) {
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    send_command(sock, "LOCK %s %s %s", repo, filename, user);
    receive_response(sock);
    close(sock);
}

// ==================== UNLOCK ====================
void cmd_unlock(const char *host, int port, const char *repo, const char *filename, const char *user) {
    int sock = connect_to_server(host, port);
    if (sock < 0) return;
    
    send_command(sock, "UNLOCK %s %s %s", repo, filename, user);
    receive_response(sock);
    close(sock);
}