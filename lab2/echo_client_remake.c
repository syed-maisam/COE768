// hello_client.c
// COE768 Lab 2 - Part III: Simple "Hello" TCP Client
// Connects to server, reads until the server closes, prints to stdout, then exits.

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
    if (argc != 3) {
        fprintf(stderr, "Client Using: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid port '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Error: invalid IPv4 address '%s'\n", server_ip);
        close(sd);
        return EXIT_FAILURE;
    }

    if (connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sd);
        return EXIT_FAILURE;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(sd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(STDOUT_FILENO, buf + written, (size_t)(n - written));
            if (w < 0) {
                if (errno == EINTR) continue;
                perror("write(stdout)");
                close(sd);
                return EXIT_FAILURE;
            }
            written += w;
        }
    }

    if (n < 0) {
        perror("read");
        close(sd);
        return EXIT_FAILURE;
    }

    close(sd);
    return EXIT_SUCCESS;
}
