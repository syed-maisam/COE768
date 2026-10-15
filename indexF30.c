#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFLEN 100
#define MAX_CONTENT 100

// Structure to store registered content
struct content_entry {
    char peer_name[20];
    char content_name[20];
    char ip_address[16];
    int port;
};

struct content_entry content_table[MAX_CONTENT];
int content_count = 0;

// Function to handle registration (R)
void handle_registration(const char *peer_name, const char *content_name, const char *ip_address, int port, int sockfd, struct sockaddr_in *client_addr, socklen_t client_len) {
    int i;

    // Debug: Log received request
    printf("Received registration request: Peer=%s, Content=%s\n", peer_name, content_name);

    // Check if the content is already registered
    for (i = 0; i < content_count; i++) {
        if (strcmp(content_table[i].content_name, content_name) == 0) {
            // Overwrite the existing entry
            strcpy(content_table[i].peer_name, peer_name);
            strcpy(content_table[i].ip_address, ip_address);
            content_table[i].port = port;

            char ack_msg[BUFLEN] = "A Registration updated successfully";
            sendto(sockfd, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)client_addr, client_len);
            return;
        }
    }

    // Register the content if not found
    strcpy(content_table[content_count].peer_name, peer_name);
    strcpy(content_table[content_count].content_name, content_name);
    strcpy(content_table[content_count].ip_address, ip_address);
    content_table[content_count].port = port;
    content_count++;

    // Respond with acknowledgment (A)
    char ack_msg[BUFLEN] = "A Registration successful";
    sendto(sockfd, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)client_addr, client_len);
}

// Function to handle search (S)
void handle_search(const char *content_name, int sockfd, struct sockaddr_in *client_addr, socklen_t client_len) {
    int i;

    // Debug: Log received search request
    printf("Received search request for content: %s\n", content_name);

    // Search for the content
    for (i = 0; i < content_count; i++) {
        if (strcmp(content_table[i].content_name, content_name) == 0) {
            // Respond with the content's peer details
            char response[BUFLEN];
            snprintf(response, BUFLEN, "S %s %s %d", content_table[i].peer_name, content_table[i].ip_address, content_table[i].port);
            sendto(sockfd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
            return;
        }
    }

    // If not found, respond with an error (E)
    char error_msg[BUFLEN] = "E Content not found";
    sendto(sockfd, error_msg, strlen(error_msg), 0, (struct sockaddr *)client_addr, client_len);
}

// Function to handle list (L)
void handle_list(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len) {
    // Debug: Log received list request
    printf("Received list request\n");

    if (content_count == 0) {
        char error_msg[BUFLEN] = "E No content registered";
        sendto(sockfd, error_msg, strlen(error_msg), 0, (struct sockaddr *)client_addr, client_len);
        return;
    }

    char response[BUFLEN * MAX_CONTENT] = ""; // Buffer to hold the list
    int i = 0;
    for (i = 0; i < content_count; i++) {
        char entry[BUFLEN];
        snprintf(entry, BUFLEN, "%s %s %s %d\n", content_table[i].content_name, content_table[i].peer_name, content_table[i].ip_address, content_table[i].port);
        strcat(response, entry);
    }

    // Send the list back to the client
    sendto(sockfd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
}

// Function to handle deregistration (D)
void handle_deregistration(const char *peer_name, int sockfd, struct sockaddr_in *client_addr, socklen_t client_len) {
    int i; // Declare variables at the top of the function
    int shift = 0;
    printf("Received deregistration request from peer: %s\n", peer_name);

    // Loop through the content table and remove all entries for the given peer
    for (i = 0; i < content_count; ) {
        if (strcmp(content_table[i].peer_name, peer_name) == 0) {
            // Shift remaining entries to overwrite the current entry
            int j;
            for (j = i; j < content_count - 1; j++) {
                content_table[j] = content_table[j + 1];
            }
            content_count--; // Reduce the content count
            shift = 1;
        } else {
            i++; // Only increment if no shift was made
        }
    }

    // Respond with appropriate message
    if (shift) {
        char ack_msg[BUFLEN] = "A Deregistration successful";
        sendto(sockfd, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)client_addr, client_len);
        printf("Deregistration completed for peer: %s\n", peer_name);
    } else {
        char error_msg[BUFLEN] = "E No content found for deregistration";
        sendto(sockfd, error_msg, strlen(error_msg), 0, (struct sockaddr *)client_addr, client_len);
        printf("No content found for peer: %s\n", peer_name);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFLEN];

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket to the port
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Index server is running on port %d...\n", port);

    while (1) {
        // Receive a PDU from a peer
        memset(buffer, 0, BUFLEN);
        int n = recvfrom(sockfd, buffer, BUFLEN, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) {
            perror("Receive failed");
            continue;
        }
        buffer[n] = '\0'; // Null-terminate the received message

        // Parse the PDU
        char type = buffer[0];
        if (type == 'R') { // Registration
            char peer_name[20], content_name[20];
            int peer_port;
            sscanf(buffer + 1, "%s %s %d", peer_name, content_name, &peer_port);
            handle_registration(peer_name, content_name, inet_ntoa(client_addr.sin_addr), peer_port, sockfd, &client_addr, client_len);
        } else if (type == 'S') { // Search
            char content_name[20];
            sscanf(buffer + 1, "%s", content_name);
            handle_search(content_name, sockfd, &client_addr, client_len);
        } else if (type == 'L') { // List
            handle_list(sockfd, &client_addr, client_len);
        } else if (type == 'D') { // Deregistration
            char peer_name[20];
            sscanf(buffer + 1, "%s", peer_name);
            handle_deregistration(peer_name, sockfd, &client_addr, client_len);

        } else {
            // Respond with an error for unsupported commands
            char error_msg[BUFLEN] = "E Unsupported command";
            sendto(sockfd, error_msg, strlen(error_msg), 0, (struct sockaddr *)&client_addr, client_len);
        }
    }

    close(sockfd);
    return 0;
}
