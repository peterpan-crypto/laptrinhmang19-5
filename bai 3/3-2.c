#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 10
#define BUFFER_SIZE 1024
#define MAX_QUEUE 100

int queue[MAX_QUEUE];
int front = 0;
int rear = 0;
int count = 0;

typedef struct {
    int client1;
    int client2;
} ChatPair;

void enqueue(int client_sock) {
    if (count == MAX_QUEUE) {
        printf("Queue is full\n");
        close(client_sock);
        return;
    }

    queue[rear] = client_sock;
    rear = (rear + 1) % MAX_QUEUE;
    count++;
}

int dequeue() {
    if (count == 0) {
        return -1;
    }

    int client_sock = queue[front];
    front = (front + 1) % MAX_QUEUE;
    count--;

    return client_sock;
}

int send_all(int sock, const char *buffer, int length) {
    int total_sent = 0;

    while (total_sent < length) {
        int sent = send(sock, buffer + total_sent, length - total_sent, 0);

        if (sent <= 0) {
            return -1;
        }

        total_sent += sent;
    }

    return total_sent;
}

void *chat_thread(void *arg) {
    ChatPair *pair = (ChatPair *)arg;

    int client1 = pair->client1;
    int client2 = pair->client2;

    free(pair);

    char *message = "You have been paired. Start chatting now.\n";
    send_all(client1, message, strlen(message));
    send_all(client2, message, strlen(message));

    printf("Paired two clients: %d and %d\n", client1, client2);

    fd_set readfds;
    char buffer[BUFFER_SIZE];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client1, &readfds);
        FD_SET(client2, &readfds);

        int max_fd = client1 > client2 ? client1 : client2;

        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(client1, &readfds)) {
            memset(buffer, 0, sizeof(buffer));

            int bytes_read = recv(client1, buffer, sizeof(buffer), 0);

            if (bytes_read <= 0) {
                printf("Client %d disconnected\n", client1);
                break;
            }

            if (send_all(client2, buffer, bytes_read) < 0) {
                printf("Cannot send message to client %d\n", client2);
                break;
            }
        }

        if (FD_ISSET(client2, &readfds)) {
            memset(buffer, 0, sizeof(buffer));

            int bytes_read = recv(client2, buffer, sizeof(buffer), 0);

            if (bytes_read <= 0) {
                printf("Client %d disconnected\n", client2);
                break;
            }

            if (send_all(client1, buffer, bytes_read) < 0) {
                printf("Cannot send message to client %d\n", client1);
                break;
            }
        }
    }

    char *close_message = "Your chat partner disconnected. Connection closed.\n";

    send_all(client1, close_message, strlen(close_message));
    send_all(client2, close_message, strlen(close_message));

    close(client1);
    close(client2);

    printf("Chat room closed\n");

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

        printf("New client connected: %s, socket = %d\n",
               inet_ntoa(client_addr.sin_addr),
               client_sock);

        enqueue(client_sock);

        char *waiting_message = "Waiting for another client...\n";
        send_all(client_sock, waiting_message, strlen(waiting_message));

        printf("Current clients in queue: %d\n", count);

        if (count >= 2) {
            int client1 = dequeue();
            int client2 = dequeue();

            ChatPair *pair = malloc(sizeof(ChatPair));

            if (pair == NULL) {
                printf("malloc failed\n");
                close(client1);
                close(client2);
                continue;
            }

            pair->client1 = client1;
            pair->client2 = client2;

            pthread_t tid;

            if (pthread_create(&tid, NULL, chat_thread, pair) != 0) {
                perror("pthread_create");
                close(client1);
                close(client2);
                free(pair);
                continue;
            }

            pthread_detach(tid);
        }
    }

    close(server_sock);
    return 0;
}
