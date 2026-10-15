#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_DATA 100

struct PDU {
    char type;
    char data[MAX_DATA];
};

void download_file(char *ip, int port, char *filename) {
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server.sin_addr);

    if (connect(sd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Failed to connect to content server\n");
        return;
    }

    struct PDU d = {'D', ""};
    strcpy(d.data, filename);

    send(sd, &d, sizeof(d), 0);

    FILE *fp = fopen(filename, "wb");
    struct PDU incoming;

    while (1) {
        int n = recv(sd, &incoming, sizeof(incoming), 0);
        if (n <= 0) break;
        fwrite(incoming.data, 1, n - 1, fp);
    }

    fclose(fp);
    close(sd);

    printf("Download complete: %s\n", filename);
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        printf("Usage: %s <peername> <index_ip> <index_port>\n", argv[0]);
        return 1;
    }

    char peername[10];
    strcpy(peername, argv[1]);

    char *index_ip = argv[2];
    int index_port = atoi(argv[3]);

    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in index;
    index.sin_family = AF_INET;
    index.sin_port = htons(index_port);
    inet_pton(AF_INET, index_ip, &index.sin_addr);

    printf("Peer %s running...\n", peername);

    while (1) {
        char cmd[50];
        printf("> ");
        fgets(cmd, sizeof(cmd), stdin);

        /* ----------------------- REGISTER ----------------------- */
        if (strncmp(cmd, "register", 8) == 0) {
            char content[10];
            sscanf(cmd, "register %s", content);

            char ip[16] = "127.0.0.1";
            int port = 9000 + rand() % 500;

            char msg[100];
            sprintf(msg, "%s %s %s %d", peername, content, ip, port);

            struct PDU r = {'R', ""};
            strcpy(r.data, msg);

            sendto(udp, &r, sizeof(r), 0, (struct sockaddr*)&index, sizeof(index));

            struct PDU reply;
            socklen_t len = sizeof(index);
            recvfrom(udp, &reply, sizeof(reply), 0, (struct sockaddr*)&index, &len);

            printf("Server: %s\n", reply.data);
        }

        /* ----------------------- SEARCH ------------------------- */
        else if (strncmp(cmd, "search", 6) == 0) {
            char content[10];
            sscanf(cmd, "search %s", content);

            struct PDU s = {'S', ""};
            strcpy(s.data, content);

            sendto(udp, &s, sizeof(s), 0, (struct sockaddr*)&index, sizeof(index));

            struct PDU reply;
            socklen_t len = sizeof(index);
            recvfrom(udp, &reply, sizeof(reply), 0, (struct sockaddr*)&index, &len);

            if (reply.type == 'E') {
                printf("Content not found.\n");
            } else {
                char ip[16];
                int port;
                sscanf(reply.data, "%s %d", ip, &port);

                printf("Downloading from %s:%d...\n", ip, port);
                download_file(ip, port, content);
            }
        }
    }

    return 0;
}
