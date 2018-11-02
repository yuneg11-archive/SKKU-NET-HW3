#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_MESSAGE_LEN 512

int createAndBindSocket(int port) {
    struct sockaddr_in local_addr;
    int sockfd;

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error: Socket creating failed");
        return -1;
    }
    if(bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("Error: Socket binding failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int receiveFromClient(int sockfd, struct sockaddr_in *client_addr_p, char *buf, int buf_len) {
    int client_addr_size = sizeof(*client_addr_p);
    int recv_size;

    if((recv_size = recvfrom(sockfd, buf, buf_len, 0, (struct sockaddr*)client_addr_p, &client_addr_size)) < 0) {
        perror("Error: Message receiving failed");
        return -1;
    }

    return recv_size;
}

int sendToClient(int sockfd, struct sockaddr_in *client_addr_p, char *buf, int buf_len) {
    int send_size;

    if((send_size = sendto(sockfd, buf, buf_len, 0, (struct sockaddr*)client_addr_p, sizeof(*client_addr_p))) < 0) {
        perror("Error: Message sending failed");
        return -1;
    }

    return send_size;
}

int main(int argc, char *argv[]) {
    int socketDescriptor;
    int port;
    struct sockaddr_in clientAddr;
    char message[MAX_MESSAGE_LEN];

    if(argc != 2) {
        printf("Usage: %s [port]\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);

    if((socketDescriptor = createAndBindSocket(port)) == -1) {
        perror("Error: Socket failed");
        exit(1);
    }

    printf("Waiting on port %d...\n", port);

    if(receiveFromClient(socketDescriptor, &clientAddr, message, sizeof(message)) == -1) {
        perror("Error: Hello message receiving failed");
        exit(1);
    }

    printf("Client: %s\n", message);

    if(sendToClient(socketDescriptor, &clientAddr, "Hello client.", 14) == -1) {
        perror("Error: Hello message sending failed");
        exit(1);
    }

    printf("Hello to client...\n");

    return 0;
}