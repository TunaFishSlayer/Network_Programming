#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>

#define MAXLINE 4096   /* max text line length */
#define SERV_PORT 3000 /* port */
#define LISTENQ 8      /* maximum number of client connections */


void sig_chld(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("child %d terminated\n", pid);
    }
    return;
}

int main(int argc, char **argv) {
    int listenfd, connfd, n;
    pid_t childpid;
    socklen_t clilen;
    char buf[MAXLINE];
    struct sockaddr_in cliaddr, servaddr;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Problem in creating the socket");
        exit(1);
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        exit(2);
    }

    if (listen(listenfd, LISTENQ) < 0) {
        perror("Listen failed");
        exit(3);
    }

    printf("Server running...waiting for connections.\n");

    signal(SIGCHLD, sig_chld);

    for (;;) {
        clilen = sizeof(cliaddr);
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0) {
            perror("Accept error");
            continue;
        }

        printf("Received request...\n");

        if ((childpid = fork()) == 0) {
            printf("Child created for dealing with client requests\n");
            close(listenfd); 

            while ((n = recv(connfd, buf, MAXLINE, 0)) > 0) {
                buf[n] = '\0';
                printf("String received and resent to client: %s\n", buf);
                send(connfd, buf, n, 0);
            }

            if (n < 0)
                perror("Read error");

            close(connfd);
            exit(0);
        }

        close(connfd);
    }
}
