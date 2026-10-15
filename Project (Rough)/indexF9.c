/* COE768 P2P Project - Index Server (C89 Compatible) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_UDP_PORT 5000
#define MAX_REG 100

struct PDU {
    char type;
    char data[100];
};

typedef struct {
    int  valid;
    char peer[11];
    char content[11];
    char ip[16];
    char port[6];
    int  used_count;
} reg_entry;

reg_entry table[MAX_REG];

void handle_register(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu);
void handle_search(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu);
void handle_list(int sd, struct sockaddr_in *client, socklen_t len);
void handle_deregister(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu);

int main(int argc, char **argv)
{
    int sd;
    int port;
    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t client_len;
    struct PDU pdu;
    ssize_t n;

    if (argc == 1) {
        port = SERVER_UDP_PORT;
    } else {
        port = atoi(argv[1]);
    }

    memset(table, 0, sizeof(table));

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd == -1) {
        perror("socket");
        exit(1);
    }

    memset(&server, 0, sizeof(server));
    server.sin_family      = AF_INET;
    server.sin_port        = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("bind");
        exit(1);
    }

    printf("Index server running on UDP port %d\n", port);

    while (1) {
        client_len = sizeof(client);
        n = recvfrom(sd, &pdu, sizeof(pdu), 0,
                     (struct sockaddr *)&client, &client_len);
        if (n <= 0) {
            continue;
        }

        switch (pdu.type) {
        case 'R':
            handle_register(sd, &client, client_len, &pdu);
            break;
        case 'S':
            handle_search(sd, &client, client_len, &pdu);
            break;
        case 'O':
            handle_list(sd, &client, client_len);
            break;
        case 'T':
            handle_deregister(sd, &client, client_len, &pdu);
            break;
        default: {
            struct PDU out;
            out.type = 'E';
            snprintf(out.data, sizeof(out.data),
                     "Unknown PDU type '%c'", pdu.type);
            sendto(sd, &out, sizeof(out), 0,
                   (struct sockaddr *) &client, client_len);
            break;
        }
        }
    }

    close(sd);
    return 0;
}

void handle_register(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu)
{
    struct PDU out;
    char peer[32];
    char content[32];
    char ip[32];
    char port[16];
    int i;
    int slot;

    if (sscanf(in_pdu->data, "%31s %31s %31s %15s",
               peer, content, ip, port) != 4) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "Bad R format. Use: peer content ip port");
        sendto(sd, &out, sizeof(out), 0,
               (struct sockaddr *)client, len);
        return;
    }

    for (i = 0; i < MAX_REG; i++) {
        if (table[i].valid &&
            strcmp(table[i].peer, peer) == 0 &&
            strcmp(table[i].content, content) == 0) {
            out.type = 'E';
            snprintf(out.data, sizeof(out.data),
                     "Peer '%s' already registered '%s'",
                     peer, content);
            sendto(sd, &out, sizeof(out), 0,
                   (struct sockaddr *)client, len);
            return;
        }
    }

    slot = -1;
    for (i = 0; i < MAX_REG; i++) {
        if (!table[i].valid) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "Registration table full");
        sendto(sd, &out, sizeof(out), 0,
               (struct sockaddr *)client, len);
        return;
    }

    table[slot].valid = 1;
    strncpy(table[slot].peer, peer, 10);
    table[slot].peer[10] = '\0';
    strncpy(table[slot].content, content, 10);
    table[slot].content[10] = '\0';
    strncpy(table[slot].ip, ip, 15);
    table[slot].ip[15] = '\0';
    strncpy(table[slot].port, port, 5);
    table[slot].port[5] = '\0';
    table[slot].used_count = 0;

    printf("REGISTER: peer=%s content=%s ip=%s port=%s (slot %d)\n",
           table[slot].peer, table[slot].content,
           table[slot].ip, table[slot].port, slot);

    out.type = 'A';
    snprintf(out.data, sizeof(out.data),
             "Registered %s:%s", peer, content);
    sendto(sd, &out, sizeof(out), 0,
           (struct sockaddr *)client, len);
}

void handle_search(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu)
{
    struct PDU out;
    char requester[32];
    char content[32];
    int i;
    int best;

    if (sscanf(in_pdu->data, "%31s %31s", requester, content) != 2) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "Bad S format. Use: peer content");
        sendto(sd, &out, sizeof(out), 0,
               (struct sockaddr *)client, len);
        return;
    }

    best = -1;
    for (i = 0; i < MAX_REG; i++) {
        if (table[i].valid &&
            strcmp(table[i].content, content) == 0) {
            if (best == -1 ||
                table[i].used_count < table[best].used_count) {
                best = i;
            }
        }
    }

    if (best == -1) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "Content '%s' not found", content);
        sendto(sd, &out, sizeof(out), 0,
               (struct sockaddr *)client, len);
        return;
    }

    table[best].used_count++;

    out.type = 'S';
    snprintf(out.data, sizeof(out.data), "%s %s %s %s",
             table[best].peer,
             table[best].content,
             table[best].ip,
             table[best].port);

    printf("SEARCH: %s requested '%s' -> %s:%s (%s:%s used=%d)\n",
           requester, content,
           table[best].peer, table[best].content,
           table[best].ip, table[best].port,
           table[best].used_count);

    sendto(sd, &out, sizeof(out), 0,
           (struct sockaddr *)client, len);
}

void handle_list(int sd, struct sockaddr_in *client, socklen_t len)
{
    struct PDU out;
    int i;
    char line[64];

    out.type = 'O';
    out.data[0] = '\0';

    for (i = 0; i < MAX_REG; i++) {
        if (table[i].valid) {
            snprintf(line, sizeof(line), "%s %s %s %s\n",
                     table[i].peer, table[i].content,
                     table[i].ip, table[i].port);

            if (strlen(out.data) + strlen(line) < sizeof(out.data)) {
                strcat(out.data, line);
            } else {
                break;
            }
        }
    }

    if (out.data[0] == '\0') {
        snprintf(out.data, sizeof(out.data),
                 "No content registered\n");
    }

    sendto(sd, &out, sizeof(out), 0,
           (struct sockaddr *)client, len);
}

void handle_deregister(int sd, struct sockaddr_in *client, socklen_t len, struct PDU *in_pdu)
{
    struct PDU out;
    char peer[32];
    char content[32];
    int i;
    int found;

    if (sscanf(in_pdu->data, "%31s %31s", peer, content) != 2) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "Bad T format. Use: peer content");
        sendto(sd, &out, sizeof(out), 0,
               (struct sockaddr *)client, len);
        return;
    }

    found = 0;
    for (i = 0; i < MAX_REG; i++) {
        if (table[i].valid &&
            strcmp(table[i].peer, peer) == 0 &&
            strcmp(table[i].content, content) == 0) {
            table[i].valid = 0;
            found = 1;
        }
    }

    if (!found) {
        out.type = 'E';
        snprintf(out.data, sizeof(out.data),
                 "No entry for %s:%s", peer, content);
    } else {
        out.type = 'A';
        snprintf(out.data, sizeof(out.data),
                 "Deregistered %s:%s", peer, content);
    }

    sendto(sd, &out, sizeof(out), 0,
           (struct sockaddr *)client, len);
}
