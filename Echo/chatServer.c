#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#define PORT 3000
#define MAX_CLIENTS 10
#define BUF_SIZE 1024

int clients[MAX_CLIENTS];
int client_count = 0;

void broadcast_message(const char *msg, int sender_fd) {
    for (int i = 0; i < client_count; i++) {
        int fd = clients[i];
        if (fd != sender_fd) { 
            send(fd, msg, strlen(msg), 0);
        }
    }
}

void remove_client(int fd) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == fd) {
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
}

int main() {
    int listenfd, maxfd, newfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    char buffer[BUF_SIZE], notice[BUF_SIZE];
    fd_set master_set, read_fds;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Chat server started on port %d...\n", PORT);

    FD_ZERO(&master_set);
    FD_SET(listenfd, &master_set);
    maxfd = listenfd;

    while (1) {
        read_fds = master_set;

        int activity = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select error");
            continue;
        }

        // New connection
        if (FD_ISSET(listenfd, &read_fds)) {
            clilen = sizeof(cliaddr);
            newfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
            if (newfd < 0) {
                perror("accept failed");
                continue;
            }

            if (client_count >= MAX_CLIENTS) {
                printf("Too many clients, rejecting connection.\n");
                close(newfd);
                continue;
            }

            clients[client_count++] = newfd;
            FD_SET(newfd, &master_set);
            if (newfd > maxfd)
                maxfd = newfd;

            snprintf(notice, sizeof(notice),
                     "New client connected: %s:%d\n",
                     inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
            printf("%s", notice);
            broadcast_message(notice, newfd);
        }

        // Check all clients for data
        for (int i = 0; i < client_count; i++) {
            int fd = clients[i];
            if (FD_ISSET(fd, &read_fds)) {
                int n = recv(fd, buffer, BUF_SIZE - 1, 0);
                if (n <= 0) {
                    // Disconnected
                    getpeername(fd, (struct sockaddr *)&cliaddr, &clilen);
                    printf("Client %s:%d disconnected.\n",
                           inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
                    snprintf(notice, sizeof(notice),
                             "Client %s:%d has left the chat.\n",
                             inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
                    broadcast_message(notice, fd);

                    close(fd);
                    FD_CLR(fd, &master_set);
                    remove_client(fd);
                    i--;
                } else {
                    buffer[n] = '\0';
                    getpeername(fd, (struct sockaddr *)&cliaddr, &clilen);
                    snprintf(notice, sizeof(notice),
                             "[%s:%d]: %s",
                             inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), buffer);
                    printf("%s", notice);
                    broadcast_message(notice, fd);
                }
            }
        }
    }

    return 0;
}
