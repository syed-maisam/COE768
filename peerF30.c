#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFLEN 100

// Function to register content with the index server
void register_content(const char *content_name, const char *peer_name, int sockfd, struct sockaddr_in *server_addr, int tcp_port) {
    char request[BUFLEN];
    snprintf(request, sizeof(request), "R %s %s %d", peer_name, content_name, tcp_port);

    // Send the registration request
    sendto(sockfd, request, strlen(request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Registration request sent for content: %s\n", content_name);

    // Wait for acknowledgment
    char response[BUFLEN];
    socklen_t server_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, response, BUFLEN, 0, (struct sockaddr *)server_addr, &server_len);
    if (n > 0) {
        response[n] = '\0'; // Null-terminate the response
        printf("Server Response: %s\n", response);
    } else {
        perror("recvfrom failed");
    }
}

// Function to request a list of all registered content
void list_content(int sockfd, struct sockaddr_in *server_addr) {
    char request[BUFLEN] = "L";

    // Send the list request
    sendto(sockfd, request, strlen(request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("List request sent to server\n");

    // Wait for the response
    char response[BUFLEN * 100]; // Use a sufficiently large buffer
    socklen_t server_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, response, sizeof(response), 0, (struct sockaddr *)server_addr, &server_len);
    if (n > 0) {
        response[n] = '\0'; // Null-terminate the response
        printf("Server Response:\n%s\n", response);
    } else {
        perror("recvfrom failed");
    }
}

// Function for Peer2 to download a file from Peer1
void download_file(const char *content_name, const char *server_ip, int server_port, int udp_sockfd, struct sockaddr_in *index_server_addr, const char *peer_name) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address.\n");
        close(sockfd);
        return;
    }

    // Connect to Peer1
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Peer1 failed");
        close(sockfd);
        return;
    }

    // Send file request
    send(sockfd, content_name, strlen(content_name), 0);
    printf("File request sent: %s\n", content_name);

    // Receive and save the file
    char buffer[BUFLEN];
    FILE *file = fopen(content_name, "w");
    if (file == NULL) {
        perror("Error creating file");
        close(sockfd);
        return;
    }

    int n;
    while ((n = recv(sockfd, buffer, BUFLEN, 0)) > 0) {
        fwrite(buffer, 1, n, file);
    }
    fclose(file);
    close(sockfd);

    if (n < 0) {
        perror("Error receiving file");
    } else {
        printf("File downloaded successfully: %s\n", content_name);

        // Pass the correct serving TCP port directly********************************************
        int tcp_port = server_port; // Use the port already configured when starting the TCP server
        register_content(content_name, peer_name, udp_sockfd, index_server_addr, tcp_port);
    }
}

// Function to search for content from the index server and download it
void search_content(const char *content_name, int sockfd, struct sockaddr_in *server_addr, const char *peer_name) {
    char request[BUFLEN];
    snprintf(request, sizeof(request), "S %s", content_name);

    // Send the search request
    sendto(sockfd, request, strlen(request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Search request sent for content: %s\n", content_name);

    // Wait for the response
    char response[BUFLEN];
    socklen_t server_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, response, BUFLEN, 0, (struct sockaddr *)server_addr, &server_len);
    if (n > 0) {
        response[n] = '\0'; // Null-terminate the response
        printf("Server Response: %s\n", response);

        // Parse the response
        if (response[0] == 'S') {
            char server_ip[16];
            int server_port;
            sscanf(response + 2, "%*s %s %d", server_ip, &server_port);
            printf("Content found on server: IP=%s, Port=%d\n", server_ip, server_port);

            // Download the file and register as content server
            download_file(content_name, server_ip, server_port, sockfd, server_addr, peer_name);
        } else if (response[0] == 'E') {
            printf("Error: %s\n", response + 2); // Display error message
        } else {
            printf("Invalid response format: %s\n", response);
        }
    } else {
        perror("recvfrom failed");
    }
}

// Function for Peer1 to serve files
void serve_files(int tcp_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcp_port);

    // Bind and listen
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) < 0) {
        perror("Listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Serving files on port %d...\n", tcp_port);

    while (1) {
        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        // Receive file request
        char file_name[BUFLEN];
        int n = recv(client_fd, file_name, BUFLEN, 0);
        if (n <= 0) {
            perror("Error receiving file request");
            close(client_fd);
            continue;
        }
        file_name[n] = '\0'; // Null-terminate

        // Send the requested file
        FILE *file = fopen(file_name, "r");
        if (file == NULL) {
            perror("File not found");
            close(client_fd);
            continue;
        }

        char buffer[BUFLEN];
        while ((n = fread(buffer, 1, BUFLEN, file)) > 0) {
            send(client_fd, buffer, n, 0);
        }
        fclose(file);
        close(client_fd);
        printf("Served file: %s\n", file_name);
    }

    close(sockfd);
}
// Function to deregister content with the index server
void deregister_content(const char *peer_name, int sockfd, struct sockaddr_in *server_addr) {
    char request[BUFLEN];
    snprintf(request, sizeof(request), "D %s", peer_name);

    // Send the deregistration request
    sendto(sockfd, request, strlen(request), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Deregistration request sent for peer: %s\n", peer_name);

    // Wait for acknowledgment
    char response[BUFLEN];
    socklen_t server_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, response, BUFLEN, 0, (struct sockaddr *)server_addr, &server_len);
    if (n > 0) {
        response[n] = '\0'; // Null-terminate the response
        printf("Server Response: %s\n", response);
    } else {
        perror("recvfrom failed");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <index_server_ip> <index_server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    if (server_port <= 0) {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, peer_addr;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure the local peer address (dynamic port)
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr.s_addr = INADDR_ANY;
    peer_addr.sin_port = 0;

    if (bind(sockfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    socklen_t addr_len = sizeof(peer_addr);
    getsockname(sockfd, (struct sockaddr *)&peer_addr, &addr_len);

    // Start TCP server for file sharing
    if (fork() == 0) {
        serve_files(ntohs(peer_addr.sin_port));
        exit(0);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    char command[BUFLEN], content_name[BUFLEN], peer_name[BUFLEN];

    printf("Enter peer name: ");
    fgets(peer_name, BUFLEN, stdin);
    peer_name[strcspn(peer_name, "\n")] = '\0';

    while (1) {
        printf("\nEnter command (R=register, S=search, L=list, Q=quit): ");
        fgets(command, BUFLEN, stdin);
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "R") == 0) {
            printf("Enter content name to register: ");
            fgets(content_name, BUFLEN, stdin);
            content_name[strcspn(content_name, "\n")] = '\0';
            register_content(content_name, peer_name, sockfd, &server_addr, ntohs(peer_addr.sin_port));
        } else if (strcmp(command, "S") == 0) {
            printf("Enter content name to search: ");
            fgets(content_name, BUFLEN, stdin);
            content_name[strcspn(content_name, "\n")] = '\0';
            search_content(content_name, sockfd, &server_addr, peer_name);
        } else if (strcmp(command, "L") == 0) {
            list_content(sockfd, &server_addr);
        } else if (strcmp(command, "Q") == 0) {
            printf("Quitting...\n");

            // Deregister all content before quitting
            deregister_content(peer_name, sockfd, &server_addr);

            break; // Exit the loop after deregistering

        } else {
            printf("Invalid command. Please try again.\n");
        }
    }

    close(sockfd);
    return 0;
}
