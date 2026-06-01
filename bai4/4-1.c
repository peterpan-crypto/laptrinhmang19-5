#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT_BACKLOG 10
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define NAME_SIZE 64

typedef struct {
    int sock;
    char name[NAME_SIZE];
    int active;
} Client;

Client clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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

int valid_client_name(const char *name) {
    if (name == NULL || strlen(name) == 0) {
        return 0;
    }

    if (strlen(name) >= NAME_SIZE) {
        return 0;
    }

    for (int i = 0; name[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_' || name[i] == '-')) {
            return 0;
        }
    }

    return 1;
}

int parse_client_id(char *line, char *name) {
    char *prefix = "client_id:";
    int prefix_len = strlen(prefix);

    if (strncmp(line, prefix, prefix_len) != 0) {
        return 0;
    }

    char *p = line + prefix_len;

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (!valid_client_name(p)) {
        return 0;
    }

    strcpy(name, p);
    return 1;
}

int name_exists(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

int add_client(int sock, const char *name) {
    pthread_mutex_lock(&clients_mutex);

    if (name_exists(name)) {
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].sock = sock;
            strcpy(clients[i].name, name);
            clients[i].active = 1;

            pthread_mutex_unlock(&clients_mutex);
            return 1;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

void remove_client(int sock) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sock == sock) {
            clients[i].active = 0;
            clients[i].sock = -1;
            clients[i].name[0] = '\0';
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_message(int sender_sock, const char *sender_name, const char *message) {
    char output[BUFFER_SIZE + NAME_SIZE + 10];
    int receiver_socks[MAX_CLIENTS];
    int receiver_count = 0;

    snprintf(output, sizeof(output), "%s: %s\n", sender_name, message);

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sock != sender_sock) {
            receiver_socks[receiver_count++] = clients[i].sock;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < receiver_count; i++) {
        send_all(receiver_socks[i], output, strlen(output));
    }
}

void *handle_client(void *arg) {
    int client_sock = *((int *)arg);
    free(arg);

    char line[BUFFER_SIZE];
    char client_name[NAME_SIZE];

    send_text(client_sock, "Enter your name using syntax: client_id: client_name\n");

    while (1) {
        ssize_t n = read_line(client_sock, line, sizeof(line));

        if (n <= 0) {
            close(client_sock);
            return NULL;
        }

        if (!parse_client_id(line, client_name)) {
            send_text(client_sock, "Wrong syntax. Please use: client_id: client_name\n");
            continue;
        }

        int result = add_client(client_sock, client_name);

        if (result == 1) {
            break;
        } else if (result == -1) {
            send_text(client_sock, "This client name is already used. Please enter another name.\n");
        } else {
            send_text(client_sock, "Server is full. Connection closed.\n");
            close(client_sock);
            return NULL;
        }
    }

    printf("Client joined: %s\n", client_name);
    send_text(client_sock, "Welcome to chat server. You can start chatting now.\n");

    while (1) {
        ssize_t n = read_line(client_sock, line, sizeof(line));

        if (n <= 0) {
            break;
        }

        if (strlen(line) == 0) {
            continue;
        }

        printf("%s: %s\n", client_name, line);

        broadcast_message(client_sock, client_name, line);
    }

    printf("Client disconnected: %s\n", client_name);

    remove_client(client_sock);
    close(client_sock);

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

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].active = 0;
        clients[i].name[0] = '\0';
    }

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
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, PORT_BACKLOG) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    printf("Chat server is running on port %d\n", port);
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

        printf("New connection from %s\n", inet_ntoa(client_addr.sin_addr));

        int *pclient = malloc(sizeof(int));

        if (pclient == NULL) {
            close(client_sock);
            continue;
        }

        *pclient = client_sock;

        pthread_t tid;

        if (pthread_create(&tid, NULL, handle_client, pclient) != 0) {
            perror("pthread_create");
            close(client_sock);
            free(pclient);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
