/* A simple file download server using TCP */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <strings.h>

#define SERVER_TCP_PORT 3000 /* well-known port */
#define PACKET_SIZE 100 /* packet size */

void reaper(int);

int main(int argc, char **argv)
{
    int sd, new_sd, client_len, port;
    struct sockaddr_in server, client;
    
    switch(argc){
        case 1:
            port = SERVER_TCP_PORT;
            break;
        case 2:
            port = atoi(argv[1]);
            break;
        default:
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            exit(1);
    }
    
    /* Create a stream socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Can't create a socket\n");
        exit(1);
    }
    
    /* Bind an address to the socket */
    bzero((char *)&server, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1){
        fprintf(stderr, "Can't bind name to socket\n");
        exit(1);
    }
    
    /* queue up to 5 connect requests */
    listen(sd, 5);
    (void) signal(SIGCHLD, reaper);
    
    while(1) {
        client_len = sizeof(client);
        new_sd = accept(sd, (struct sockaddr *)&client, &client_len);
        
        if(new_sd < 0){
            fprintf(stderr, "Can't accept client\n");
            exit(1);
        }
        
        switch (fork()){
            case 0:
                // -- CHILD PROCESS -- //
                (void) close(sd);
                
                char file_name[256];
                memset(file_name, 0, sizeof(file_name));
                
                // Read filename from client
                int name_len = read(new_sd, file_name, sizeof(file_name) - 1);
                if (name_len > 0) {
                    file_name[name_len] = '\0'; // Null terminate
                    printf("FILE REQUEST RECEIVED: %s\n", file_name);
                }
                
                FILE* file = fopen(file_name, "rb");
                
                if (file == NULL) {
                    // Send error message with 'E' indicator
                    printf("-- NO FILE FOUND --\n");
                    char error_msg[PACKET_SIZE];
                    error_msg[0] = 'E'; // Error indicator
                    snprintf(error_msg + 1, PACKET_SIZE - 1, "Error: File '%s' not found", file_name);
                    write(new_sd, error_msg, strlen(error_msg));
                } else {
                    // Send file data with 'F' indicator on first packet
                    printf("SUCCESSFULLY FOUND FILE: %s\n", file_name);
                    
                    char packet[PACKET_SIZE];
                    int bytes_read;
                    int first_packet = 1;
                    
                    while ((bytes_read = fread(packet, 1, PACKET_SIZE, file)) > 0) {
                        if (first_packet) {
                            // Insert 'F' indicator at the beginning
                            char temp[PACKET_SIZE + 1];
                            temp[0] = 'F'; // File data indicator
                            memcpy(temp + 1, packet, bytes_read);
                            write(new_sd, temp, bytes_read + 1);
                            printf("Sent first packet: %d bytes (+ 1 indicator byte)\n", bytes_read);
                            first_packet = 0;
                        } else {
                            write(new_sd, packet, bytes_read);
                            printf("Sent packet: %d bytes\n", bytes_read);
                        }
                    }
                    
                    fclose(file);
                    printf("File transfer complete\n");
                }
                
                (void) close(new_sd);
                exit(0);
                
            default:
                // -- PARENT PROCESS -- //
                (void) close(new_sd);
                break;
                
            case -1:
                fprintf(stderr, "fork: error\n");
        }
    }
}

/* reaper */
void reaper(int sig) {
    int status;
    while(wait3(&status, WNOHANG, (struct rusage *)0) >= 0);
}
