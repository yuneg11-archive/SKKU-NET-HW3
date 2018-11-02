#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_MESSAGE_LEN 512
#define SERVER_ADDR_LEN 128

int createAndSetSocket(char *addr, int port, struct sockaddr_in *server_addr_p) {
    int sockfd;

    memset(server_addr_p, 0, sizeof(*server_addr_p));
    server_addr_p->sin_family = AF_INET;
    server_addr_p->sin_port = htons(port);
    server_addr_p->sin_addr.s_addr = inet_addr(addr);

    if((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error: Socket creating failed");
        return -1;
    }

    return sockfd;
}

int receiveFromServer(int sockfd, struct sockaddr_in *server_addr_p, char *buf, int buf_len) {
    int server_addr_size = sizeof(*server_addr_p);
    int recv_size;

    if((recv_size = recvfrom(sockfd, buf, buf_len, 0, (struct sockaddr*)server_addr_p, &server_addr_size)) < 0) {
        perror("Error: Message receiving failed");
        return -1;
    }

    return recv_size;
}

int sendToServer(int sockfd, struct sockaddr_in *server_addr_p, char *buf, int buf_len) {
    int send_size;

    if((send_size = sendto(sockfd, buf, buf_len, 0, (struct sockaddr*)server_addr_p, sizeof(*server_addr_p))) < 0) {
        printf("Edd\n");
        perror("Error: Message sending failed");
        return -1;
    }

    return send_size;
}

int main(int argc, char *argv[]) {
    int socketDescriptor;
    char serverAddress[SERVER_ADDR_LEN];
    int port;
    struct sockaddr_in serverAddr;
    char message[MAX_MESSAGE_LEN];

    if(argc != 3) {
        printf("Usage: %s [server address] [port]\n", argv[0]);
        exit(1);
    }

    strcpy(serverAddress, argv[1]);
    port = atoi(argv[2]);

    if((socketDescriptor = createAndSetSocket(serverAddress, port, &serverAddr)) == -1) {
        perror("Error: Socket failed");
        exit(1);
    }

    printf("Hello to server %s:%d...\n", serverAddress, port);

    if(sendToServer(socketDescriptor, &serverAddr, "Hello server.", 14) == -1) {
        perror("Error: Hello message sending failed");
        exit(1);
    }

    if(receiveFromServer(socketDescriptor, &serverAddr, message, sizeof(message)) == -1) {
        perror("Error: Hello message receiving failed");
        exit(1);
    }

    printf("Server: %s\n", message);

    return 0;
}