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
#define BUFFER_SIZE 4096
#define BODY_SIZE 2048
#define QUEUE_SIZE 1024

int client_queue[QUEUE_SIZE];
int queue_front = 0;
int queue_rear = 0;
int queue_count = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;

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

void enqueue_client(int client_sock) {
    pthread_mutex_lock(&queue_mutex);

    if (queue_count == QUEUE_SIZE) {
        printf("Queue is full. Close client %d\n", client_sock);
        close(client_sock);
    } else {
        client_queue[queue_rear] = client_sock;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        queue_count++;

        pthread_cond_signal(&queue_not_empty);
    }

    pthread_mutex_unlock(&queue_mutex);
}

int dequeue_client() {
    pthread_mutex_lock(&queue_mutex);

    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_mutex);
    }

    int client_sock = client_queue[queue_front];
    queue_front = (queue_front + 1) % QUEUE_SIZE;
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);

    return client_sock;
}

void send_http_response(int client_sock, int status_code, const char *status_text, const char *body) {
    char header[BUFFER_SIZE];

    int body_len = strlen(body);

    snprintf(header,
             sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html; charset=UTF-8\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code,
             status_text,
             body_len);

    send_all(client_sock, header, strlen(header));
    send_all(client_sock, body, body_len);
}

void handle_http_client(int client_sock) {
    char request[BUFFER_SIZE];

    int received = recv(client_sock, request, sizeof(request) - 1, 0);

    if (received <= 0) {
        close(client_sock);
        return;
    }

    request[received] = '\0';

    printf("----- HTTP Request from client %d -----\n", client_sock);
    printf("%s\n", request);
    printf("--------------------------------------\n");

    char method[16];
    char path[256];
    char version[32];

    int count = sscanf(request, "%15s %255s %31s", method, path, version);

    if (count != 3) {
        char *body =
            "<html>"
            "<body>"
            "<h1>400 Bad Request</h1>"
            "<p>Invalid HTTP request.</p>"
            "</body>"
            "</html>";

        send_http_response(client_sock, 400, "Bad Request", body);
        close(client_sock);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        char *body =
            "<html>"
            "<body>"
            "<h1>405 Method Not Allowed</h1>"
            "<p>Only GET method is supported.</p>"
            "</body>"
            "</html>";

        send_http_response(client_sock, 405, "Method Not Allowed", body);
        close(client_sock);
        return;
    }

    char body[BODY_SIZE];

    snprintf(body,
             sizeof(body),
             "<html>"
             "<head>"
             "<title>Prethreaded HTTP Server</title>"
             "</head>"
             "<body>"
             "<h1>Xin chao cac ban</h1>"
             "<p>This is HTTP server using prethreading.</p>"
             "<p>Requested path: %s</p>"
             "</body>"
             "</html>",
             path);

    send_http_response(client_sock, 200, "OK", body);

    close(client_sock);
}

void *worker_thread(void *arg) {
    int thread_id = *((int *)arg);
    free(arg);

    printf("Worker thread %d started\n", thread_id);

    while (1) {
        int client_sock = dequeue_client();

        printf("Worker thread %d handles client %d\n", thread_id, client_sock);

        handle_http_client(client_sock);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <port> <number_of_threads>\n", argv[0]);
        printf("Example: %s 8080 4\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);
    int number_of_threads = atoi(argv[2]);

    if (number_of_threads <= 0) {
        printf("Number of threads must be greater than 0\n");
        return 1;
    }

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

    printf("HTTP server is running on port %d\n", port);
    printf("Using prethreading with %d worker threads\n", number_of_threads);

    for (int i = 0; i < number_of_threads; i++) {
        pthread_t tid;

        int *thread_id = malloc(sizeof(int));

        if (thread_id == NULL) {
            printf("malloc failed\n");
            close(server_sock);
            return 1;
        }

        *thread_id = i + 1;

        if (pthread_create(&tid, NULL, worker_thread, thread_id) != 0) {
            perror("pthread_create");
            free(thread_id);
            close(server_sock);
            return 1;
        }

        pthread_detach(tid);
    }

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

        enqueue_client(client_sock);
    }

    close(server_sock);

    return 0;
}
