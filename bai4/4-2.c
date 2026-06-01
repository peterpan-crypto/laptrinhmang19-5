#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 10
#define BUFFER_SIZE 2048
#define USER_SIZE 64
#define PASS_SIZE 64
#define CMD_SIZE 1024

typedef struct {
    int client_sock;
    char db_file[256];
} ClientInfo;

ssize_t send_all(int sock, const char *data, size_t len) {
    size_t total = 0;

    while (total < len) {
        ssize_t sent = send(sock, data + total, len - total, 0);

        if (sent <= 0) {
            return -1;
        }

        total += sent;
    }

    return total;
}

void send_text(int sock, const char *text) {
    send_all(sock, text, strlen(text));
}

ssize_t read_line(int sock, char *buffer, size_t size) {
    size_t i = 0;
    unsigned char c;

    while (i < size - 1) {
        ssize_t n = recv(sock, &c, 1, 0);

        if (n <= 0) {
            if (i == 0) {
                return n;
            }
            break;
        }

        /*
           Bỏ qua một số byte điều khiển của telnet.
           Nếu test bằng nc thì phần này không ảnh hưởng.
        */
        if (c == 255) {
            unsigned char tmp[2];
            recv(sock, tmp, 2, 0);
            continue;
        }

        if (c == '\n') {
            break;
        }

        if (c != '\r') {
            buffer[i++] = c;
        }
    }

    buffer[i] = '\0';
    return i;
}

int check_login(const char *db_file, const char *username, const char *password) {
    FILE *fp = fopen(db_file, "r");

    if (fp == NULL) {
        return 0;
    }

    char file_user[USER_SIZE];
    char file_pass[PASS_SIZE];

    while (fscanf(fp, "%63s %63s", file_user, file_pass) == 2) {
        if (strcmp(username, file_user) == 0 &&
            strcmp(password, file_pass) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

void execute_command_and_send_result(int client_sock, const char *command) {
    char output_file[128];
    char system_command[CMD_SIZE + 256];

    /*
       Dùng file riêng cho từng client để tránh nhiều thread ghi chung out.txt.
       Vẫn đúng ý tưởng: command > file_output
    */
    snprintf(output_file, sizeof(output_file), "/tmp/telnet_out_%d.txt", client_sock);

    snprintf(system_command,
             sizeof(system_command),
             "%s > %s 2>&1",
             command,
             output_file);

    int result = system(system_command);

    FILE *fp = fopen(output_file, "r");

    if (fp == NULL) {
        send_text(client_sock, "Cannot open output file.\n");
        return;
    }

    char buffer[BUFFER_SIZE];
    int has_output = 0;

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        has_output = 1;
        send_text(client_sock, buffer);
    }

    fclose(fp);
    remove(output_file);

    if (!has_output) {
        send_text(client_sock, "(no output)\n");
    }

    if (result != 0) {
        send_text(client_sock, "(command finished with error status)\n");
    }
}

void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;

    int client_sock = info->client_sock;
    char db_file[256];

    strcpy(db_file, info->db_file);
    free(info);

    char username[USER_SIZE];
    char password[PASS_SIZE];
    char command[CMD_SIZE];

    send_text(client_sock, "Username: ");

    if (read_line(client_sock, username, sizeof(username)) <= 0) {
        close(client_sock);
        return NULL;
    }

    send_text(client_sock, "Password: ");

    if (read_line(client_sock, password, sizeof(password)) <= 0) {
        close(client_sock);
        return NULL;
    }

    if (!check_login(db_file, username, password)) {
        send_text(client_sock, "Login failed.\n");
        close(client_sock);
        return NULL;
    }

    send_text(client_sock, "Login successful.\n");
    send_text(client_sock, "Type command to execute. Type exit to quit.\n");
    send_text(client_sock, "telnet_server> ");

    while (1) {
        ssize_t n = read_line(client_sock, command, sizeof(command));

        if (n <= 0) {
            break;
        }

        if (strlen(command) == 0) {
            send_text(client_sock, "telnet_server> ");
            continue;
        }

        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            send_text(client_sock, "Goodbye.\n");
            break;
        }

        printf("Client %d executes command: %s\n", client_sock, command);

        execute_command_and_send_result(client_sock, command);

        send_text(client_sock, "telnet_server> ");
    }

    close(client_sock);
    printf("Client %d disconnected\n", client_sock);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <port> <user_database_file>\n", argv[0]);
        printf("Example: %s 9000 users.txt\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);
    char *db_file = argv[2];

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, BACKLOG) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    printf("Telnet server is running on port %d\n", port);
    printf("User database file: %s\n", db_file);
    printf("Waiting for clients...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock,
                                 (struct sockaddr *)&client_addr,
                                 &client_len);

        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        printf("New client connected from %s, socket = %d\n",
               inet_ntoa(client_addr.sin_addr),
               client_sock);

        ClientInfo *info = malloc(sizeof(ClientInfo));

        if (info == NULL) {
            close(client_sock);
            continue;
        }

        info->client_sock = client_sock;
        strcpy(info->db_file, db_file);

        pthread_t tid;

        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create");
            close(client_sock);
            free(info);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
