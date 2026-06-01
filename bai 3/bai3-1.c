#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define DIR_PATH "./files"

void sigchld_handler(int s) {
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void trim_newline(char *str) {
    int len = strlen(str);
    while(len > 0 && (str[len-1] == '\n' || str[len-1] == '\r')) {
        str[len-1] = '\0';
        len--;
    }
}

void handle_client(int client_socket) {
    DIR *d;
    struct dirent *dir;
    int file_count = 0;
    char buffer[BUFFER_SIZE];
    
    d = opendir(DIR_PATH);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                file_count++;
            }
        }
        closedir(d);
    } else {
        perror("Could not open directory");
        file_count = 0;
    }

    if (file_count == 0) {
        char *msg = "ERROR No files to download \r\n";
        send(client_socket, msg, strlen(msg), 0);
        close(client_socket);
        return;
    }

    sprintf(buffer, "OK %d\r\n", file_count);
    send(client_socket, buffer, strlen(buffer), 0);

    d = opendir(DIR_PATH);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                sprintf(buffer, "%s\r\n", dir->d_name);
                send(client_socket, buffer, strlen(buffer), 0);
            }
        }
        closedir(d);
    }
    
    send(client_socket, "\r\n", 2, 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected or error.\n");
            break;
        }

        trim_newline(buffer);
        if (strlen(buffer) == 0) {
            continue;
        }

        printf("Client requested file: '%s'\n", buffer);

        char filepath[BUFFER_SIZE + 256];
        snprintf(filepath, sizeof(filepath), "%s/%s", DIR_PATH, buffer);

        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            long file_size = st.st_size;
            
            char header[128];
            sprintf(header, "OK %ld\r\n", file_size);
            send(client_socket, header, strlen(header), 0);

            FILE *fp = fopen(filepath, "rb");
            if (fp != NULL) {
                int bytes_read;
                while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
                    send(client_socket, buffer, bytes_read, 0);
                }
                fclose(fp);
                printf("File '%s' sent successfully.\n", filepath);
            }
            
            break;
        } else {
            char *error_msg = "ERROR File not found. Please send file name again.\r\n";
            send(client_socket, error_msg, strlen(error_msg), 0);
        }
    }

    close(client_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    struct sigaction sa;

    struct stat st = {0};
    if (stat(DIR_PATH, &st) == -1) {
        mkdir(DIR_PATH, 0700);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        printf("New client connected.\n");

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(new_socket);
        } else if (pid == 0) {
            close(server_fd);
            handle_client(new_socket);
            exit(0);
        } else {
            close(new_socket);
        }
    }

    return 0;
}
