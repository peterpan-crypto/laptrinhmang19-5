#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 10
#define BUFFER_SIZE 1024

typedef struct {
    int client_sock;
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
    char c;

    while (i < size - 1) {
        ssize_t n = recv(sock, &c, 1, 0);

        if (n <= 0) {
            if (i == 0) {
                return n;
            }
            break;
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

int get_time_format(const char *format, char *strftime_format, size_t size) {
    if (strcmp(format, "dd/mm/yyyy") == 0) {
        snprintf(strftime_format, size, "%%d/%%m/%%Y");
        return 1;
    }

    if (strcmp(format, "dd/mm/yy") == 0) {
        snprintf(strftime_format, size, "%%d/%%m/%%y");
        return 1;
    }

    if (strcmp(format, "mm/dd/yyyy") == 0) {
        snprintf(strftime_format, size, "%%m/%%d/%%Y");
        return 1;
    }

    if (strcmp(format, "mm/dd/yy") == 0) {
        snprintf(strftime_format, size, "%%m/%%d/%%y");
        return 1;
    }

    return 0;
}

void process_command(int client_sock, char *command) {
    char cmd[BUFFER_SIZE];
    char format[BUFFER_SIZE];
    char extra[BUFFER_SIZE];

    int count = sscanf(command, "%s %s %s", cmd, format, extra);

    if (count != 2) {
        send_text(client_sock, "ERROR Invalid command syntax\n");
        send_text(client_sock, "Usage: GET_TIME dd/mm/yyyy\n");
        send_text(client_sock, "Supported formats: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n");
        return;
    }

    if (strcmp(cmd, "GET_TIME") != 0) {
        send_text(client_sock, "ERROR Unknown command\n");
        send_text(client_sock, "Usage: GET_TIME [format]\n");
        return;
    }

    char strftime_format[32];

    if (!get_time_format(format, strftime_format, sizeof(strftime_format))) {
        send_text(client_sock, "ERROR Invalid time format\n");
        send_text(client_sock, "Supported formats: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n");
        return;
    }

    time_t now = time(NULL);
    struct tm *time_info = localtime(&now);

    char result[128];
    strftime(result, sizeof(result), strftime_format, time_info);

    char response[256];
    snprintf(response, sizeof(response), "OK %s\n", result);

    send_text(client_sock, response);
}

void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    int client_sock = info->client_sock;

    free(info);

    char buffer[BUFFER_SIZE];

    send_text(client_sock, "Time server connected.\n");
    send_text(client_sock, "Command: GET_TIME [format]\n");
    send_text(client_sock, "Formats: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n");
    send_text(client_sock, "Type exit to quit.\n");

    while (1) {
        send_text(client_sock, "time_server> ");

        ssize_t n = read_line(client_sock, buffer, sizeof(buffer));

        if (n <= 0) {
            break;
        }

        if (strlen(buffer) == 0) {
            continue;
        }

        if (strcmp(buffer, "exit") == 0 || strcmp(buffer, "quit") == 0) {
            send_text(client_sock, "Goodbye.\n");
            break;
        }

        printf("Client %d sent: %s\n", client_sock, buffer);

        process_command(client_sock, buffer);
    }

    close(client_sock);

    printf("Client %d disconnected\n", client_sock);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        printf("Example: %s 9000\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;

    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_sock);
        return 1;
    }

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

    printf("Time server is running on port %d\n", port);
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
