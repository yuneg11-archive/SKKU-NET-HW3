#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>

#define MAX_MESSAGE_LEN 1024//512
#define MAX_FILE_NAME_LEN 256
#define TIME_SPACE_USEC 30

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

int getFileList(char *dirname, char *list) {
    DIR *dir;
    struct dirent *dirent;
    int cnt = 0;

    dir = opendir(dirname);

    if(dir == NULL) {
        perror("Error: Directory accessing failed");
        return -1;
    }

    strcpy(list, "");

    while ((dirent = readdir(dir)) != NULL) {
        if(dirent->d_type == DT_REG) {
            cnt++;
            strcat(list, dirent->d_name);
            strcat(list, "\n");
        }
    }
    closedir(dir);

    return cnt;
}

int sendFileToClient(int sockfd, struct sockaddr_in *client_addr_p, FILE *file) {
    char buf[MAX_MESSAGE_LEN];
    int read_size;
    int total_send_size = 0;
    int send_size;
    int print_size = 0;

    while(1) {
        if((read_size = fread(buf, 1, sizeof(buf), file)) == 0) break;
        if((send_size = sendToClient(sockfd, client_addr_p, buf, read_size)) < 0) {
            perror("Error: Data streaming failed");
            return -1;
        }
        total_send_size += send_size;
        if(print_size != total_send_size / (1024*1024) + 1) {
            print_size = total_send_size / (1024*1024) + 1;
            printf("%d MB...\n", print_size);
        }
        usleep(TIME_SPACE_USEC);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int socketDescriptor;
    int port;
    struct sockaddr_in clientAddr;
    char message[MAX_MESSAGE_LEN];
    int videoCount;
    char videoDir[10] = "./video/";
    char fileName[MAX_FILE_NAME_LEN];
    FILE *filePointer;

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

    if(receiveFromClient(socketDescriptor, &clientAddr, message, sizeof(message)) == -1) {
        perror("Error: Video list request message receiving failed");
        exit(1);
    }

    printf("Client: %s\n", message);

    if(strcmp(message, "Request video list") != 0) {
        fprintf(stderr, "Error: Invalid request message received\n");
        exit(1);
    }

    if((videoCount = getFileList(videoDir, message)) < 0) {
        perror("Error: Video list accessing failed");
        sendToClient(socketDescriptor, &clientAddr, "Empty", 6);
        exit(1);
    }

    if(videoCount == 0) {
        fprintf(stderr, "Error: No video file exist\n");
        sendToClient(socketDescriptor, &clientAddr, "Empty", 6);
        exit(1);
    }

    printf("Sending video list (%d videos)...\n", videoCount);

    if(sendToClient(socketDescriptor, &clientAddr, message, strlen(message)+1) == -1) {
        perror("Error: Video list sending failed");
        exit(1);
    }

    printf("Waiting for client to select video...\n");

    if(receiveFromClient(socketDescriptor, &clientAddr, message, sizeof(message)) == -1) {
        perror("Error: Video request message receiving failed");
        exit(1);
    }

    printf("Client: %s\n", message);

    if(strncmp(message, "Request ", 8) != 0) {
        fprintf(stderr, "Error: Invalid request message received\n");
        exit(1);
    }

    strcpy(fileName, videoDir);
    strcat(fileName, &message[8]);

    if((filePointer = fopen(fileName, "r")) == NULL) {
        perror("Error: File accessing failed");
        sendToClient(socketDescriptor, &clientAddr, "File Not Available", 6);
        exit(1);
    }

    if(sendFileToClient(socketDescriptor, &clientAddr, filePointer) == -1) {
        perror("Error: Video streaming failed");
        exit(1);
    }

    printf("Video streaming complete.\n");

    fclose(filePointer);
    close(socketDescriptor);

    return 0;
}