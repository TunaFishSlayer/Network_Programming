#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAXLINE 4096
#define SERV_PORT 3000

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in servaddr;
    char buffer[MAXLINE];
    FILE *fp;
    int n;

    if (argc != 3) {
        printf("Usage: %s <server_ip> <file_path>\n", argv[0]);
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Problem in creating the socket");
        exit(2);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Problem in connecting to the server");
        exit(3);
    }

    char *filename = strrchr(argv[2], '/'); 
    if (filename) filename++;
    else filename = argv[2];

    send(sockfd, filename, strlen(filename) + 1, 0); 

    fp = fopen(argv[2], "rb");
    if (fp == NULL) {
        perror("Cannot open file");
        exit(4);
    }

    while ((n = fread(buffer, sizeof(char), MAXLINE, fp)) > 0) {
        send(sockfd, buffer, n, 0);
    }

    printf("File sent successfully!\n");

    fclose(fp);
    close(sockfd);
    return 0;
}

