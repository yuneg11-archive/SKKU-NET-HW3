#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_MESSAGE_LEN 1024//512
#define SERVER_ADDR_LEN 128
#define MAX_FILE_NAME_LEN 256
#define MAX_FILE_LIST_SIZE 256
#define STREAMING_TIMEOUT_SEC 30

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

int receiveFromServer(int sockfd, struct sockaddr_in *server_addr_p, char *buf, int buf_len, int option) {
    int server_addr_size = sizeof(*server_addr_p);
    int recv_size;

    if((recv_size = recvfrom(sockfd, buf, buf_len, option, (struct sockaddr*)server_addr_p, &server_addr_size)) < 0) {
        perror("Error: Message receiving failed");
        return -1;
    }

    return recv_size;
}

int sendToServer(int sockfd, struct sockaddr_in *server_addr_p, char *buf, int buf_len) {
    int send_size;

    if((send_size = sendto(sockfd, buf, buf_len, 0, (struct sockaddr*)server_addr_p, sizeof(*server_addr_p))) < 0) {
        perror("Error: Message sending failed");
        return -1;
    }

    return send_size;
}

char *getVideoFileNameFromUser(char *filename, char *list) {
    char *filename_list[MAX_FILE_LIST_SIZE];
    char *cur = list;
    int file_cnt = 0;
    int select;
    int i;

    do {
        filename_list[file_cnt] = cur;
        cur = strchr(cur, '\n');
        if(cur != NULL) {
            *cur = '\0';
            cur++;
            file_cnt++;
        }
    } while(cur != NULL);

    if(file_cnt == 0) {
        perror("Error: Empty file list");
        return NULL;
    }

    printf("\n=============== Video List ===============\n");
    for(i = 0; i < file_cnt; i++)
        printf(" %d) %s\n", i+1, filename_list[i]);
    do {
        printf(" - Choose number: ");
        scanf("%d", &select);
    } while(select < 1 || select > file_cnt);
    printf("==========================================\n\n");

    strcpy(filename, filename_list[select-1]);
    
    return filename;
}

int receiveFileFromServer(int sockfd, struct sockaddr_in *server_addr_p, FILE *file) {
    char buf[MAX_MESSAGE_LEN];
    int write_size;
    int total_receive_size = 0;
    int receive_size;

    struct timeval timeout;
    fd_set fds;

    timeout.tv_sec = STREAMING_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    while(1) {
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        
        if(select(sockfd+1, &fds, 0, 0, &timeout) < 0) {
            perror("Error: Data streaming failed");
            return -1;
        }

        if (FD_ISSET(sockfd, &fds)) {
            if((receive_size = receiveFromServer(sockfd, server_addr_p, buf, sizeof(buf), 0)) == -1) break;
            write_size = fwrite(buf, 1, receive_size, file);
            total_receive_size += receive_size;
            printf("%d KB...\n", total_receive_size / 1024 + 1);
        } else break;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int socketDescriptor;
    char serverAddress[SERVER_ADDR_LEN];
    int port;
    struct sockaddr_in serverAddr;
    char message[MAX_MESSAGE_LEN];
    char fileName[MAX_FILE_NAME_LEN];
    FILE *filePointer;

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

    if(receiveFromServer(socketDescriptor, &serverAddr, message, sizeof(message), 0) == -1) {
        perror("Error: Hello message receiving failed");
        exit(1);
    }

    printf("Server: %s\n", message);

    printf("Requesting video list...\n");

    if(sendToServer(socketDescriptor, &serverAddr, "Request video list", 19) == -1) {
        perror("Error: Video list request message sending failed");
        exit(1);
    }

    if(receiveFromServer(socketDescriptor, &serverAddr, message, sizeof(message), 0) == -1) {
        perror("Error: Video list receiving failed");
        exit(1);
    }

    if(strcmp(message, "Empty") == 0) {
        fprintf(stderr, "Error: No video file exist\n");
        exit(1);
    }

    if(getVideoFileNameFromUser(fileName, message) == NULL) {
        perror("Error: Video selecting failed");
        exit(1);
    }

    printf("Requesting video %s...\n", fileName);

    if((filePointer = fopen(fileName, "w")) == NULL) {
        perror("Error: File accessing failed");
        exit(1);
    }

    strcpy(message, "Request ");
    strcat(message, fileName);

    if(sendToServer(socketDescriptor, &serverAddr, message, strlen(message)+1) == -1) {
        perror("Error: Video request message sending failed");
        exit(1);
    }

    if(receiveFileFromServer(socketDescriptor, &serverAddr, filePointer) == -1) {
        perror("Error: Video streaming failed");
        exit(1);
    }

    printf("Video streaming complete.\n");

    fclose(filePointer);
    close(socketDescriptor);

    return 0;
}