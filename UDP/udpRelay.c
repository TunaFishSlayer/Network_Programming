#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SERV_PORT 1255
#define MAXLINE 255

int main() {
    int sockfd, n;
    int len;
    char mesg[MAXLINE]; 
    struct sockaddr_in servaddr, cliaddr, client1, client2;
    int client_count = 0;
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    memset(&servaddr, 0, sizeof(servaddr)); 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(SERV_PORT); 

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == 0) {
    printf("Server is running at port %d\n", SERV_PORT);
    } else {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

for (;;) {
    len = sizeof(cliaddr);
    n = recvfrom(sockfd, mesg, MAXLINE, 0,
                 (struct sockaddr *)&cliaddr, &len);
    if (n == SOCKET_ERROR) {
        printf("recvfrom failed: %d\n", WSAGetLastError());
        break;
    }
    mesg[n] = '\0';

    // Register clients
    if (client_count < 2) {
        if (client_count == 0) {
            client1 = cliaddr;
            client_count++;
            printf("Client1 registered: %s:%d\n",
                   inet_ntoa(client1.sin_addr), ntohs(client1.sin_port));
        } else if (client_count == 1 &&
                   (cliaddr.sin_addr.s_addr != client1.sin_addr.s_addr ||
                    cliaddr.sin_port != client1.sin_port)) {
            client2 = cliaddr;
            client_count++;
            printf("Client2 registered: %s:%d\n",
                   inet_ntoa(client2.sin_addr), ntohs(client2.sin_port));
        }
    }

    printf("Received from %s:%d: %s\n",
           inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), mesg);

    // Relay
    if (client_count == 2) {
        if (cliaddr.sin_addr.s_addr == client1.sin_addr.s_addr &&
            cliaddr.sin_port == client1.sin_port) {
            sendto(sockfd, mesg, n, 0, (struct sockaddr *)&client2, sizeof(client2));
            printf("Forwarded to Client2\n");
        } else {
            sendto(sockfd, mesg, n, 0, (struct sockaddr *)&client1, sizeof(client1));
            printf("Forwarded to Client1\n");
        }
    } else {
        printf("Waiting for 2nd client to connect...\n");
    }
    }
    closesocket(sockfd);
    WSACleanup();
    return 0;
}
