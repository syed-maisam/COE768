// hello_server.c
// COE768 Lab 2 - Part III: Simple "Hello" TCP Server
// After a client connects, send "Hello\n", then close and exit.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid port '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Allow quick reuse of the port if the server is restarted
    int optval = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(sd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // bind on all interfaces
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sd);
        return EXIT_FAILURE;
    }

    if (listen(sd, 5) < 0) {
        perror("listen");
        close(sd);
        return EXIT_FAILURE;
    }

    printf("Echo Server: Connected to port %d \n", port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int new_sd = accept(sd, (struct sockaddr *)&client_addr, &client_len);
    if (new_sd < 0) {
        perror("accept");
        close(sd);
        return EXIT_FAILURE;
    }

    char client_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip))) {
        printf("Echo Server: client connected from %s:%d\n",
               client_ip, ntohs(client_addr.sin_port));
    }

    const char msg[] = "Hello\n";
    size_t to_write = sizeof(msg) - 1;
    const char *p = msg;
    while (to_write > 0) {
        ssize_t n = write(new_sd, p, to_write);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("write");
            close(new_sd);
            close(sd);
            return EXIT_FAILURE;
        }
        to_write -= (size_t)n;
        p += n;
    }

    // Politely half-close the write side, then close.
    shutdown(new_sd, SHUT_WR);
    close(new_sd);
    close(sd);
    printf("Echo Server: sent 'Hello' and closed the connection.\n");
    return EXIT_SUCCESS;
}
