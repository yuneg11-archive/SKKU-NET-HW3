#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define MAX_MESSAGE_LEN 1024//512
#define SERVER_ADDR_LEN 128
#define MAX_FILE_NAME_LEN 256
#define MAX_FILE_LIST_SIZE 256
#define TIME_SPACE_USEC 30
#define STREAMING_TIMEOUT_SEC 3

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
    socklen_t server_addr_size = sizeof(*server_addr_p);
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
    socklen_t server_addr_size = sizeof(*server_addr_p);
    char buf[MAX_MESSAGE_LEN];
    int receive_size;
    struct timeval start_time, current_time, interval;

    gettimeofday(&start_time, NULL);
    while(1) {
        if((receive_size = recvfrom(sockfd, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr*)server_addr_p, &server_addr_size)) == -1) {
            gettimeofday(&current_time, NULL);
            timersub(&current_time, &start_time, &interval);
            if(interval.tv_sec > STREAMING_TIMEOUT_SEC) break;
            usleep(TIME_SPACE_USEC);
        } else {
            fwrite(buf, 1, receive_size, file);
            gettimeofday(&start_time, NULL);
        }
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

    if(receiveFromServer(socketDescriptor, &serverAddr, message, sizeof(message)) == -1) {
        perror("Error: Hello message receiving failed");
        exit(1);
    }

    printf("Server: %s\n", message);

    printf("Requesting video list...\n");

    if(sendToServer(socketDescriptor, &serverAddr, "Request video list", 19) == -1) {
        perror("Error: Video list request message sending failed");
        exit(1);
    }

    if(receiveFromServer(socketDescriptor, &serverAddr, message, sizeof(message)) == -1) {
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

    printf("Streaming video...\n");

    if(receiveFileFromServer(socketDescriptor, &serverAddr, filePointer) == -1) {
        perror("Error: Video streaming failed");
        exit(1);
    }

    printf("Video streaming complete.\n");

    fclose(filePointer);
    close(socketDescriptor);

    return 0;
}