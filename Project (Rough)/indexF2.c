#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_DATA 100

struct PDU {
    char type;
    char data[MAX_DATA];
};

typedef struct {
    char peer[10];
    char content[10];
    char ip[16];
    int port;
    int used;
} entry;

entry table[100];
int count = 0;

int main(int argc, char *argv[]) {

    int sd, port;
    struct sockaddr_in server, client;
    socklen_t len;
    struct PDU pdu;

    /* loop counters */
    int i, best;

    /* temporary parsing variables */
    char peer[10], content[10], ip[16];
    int pnum;

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    sd = socket(AF_INET, SOCK_DGRAM, 0);

    len = sizeof(client);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    bind(sd, (struct sockaddr*)&server, sizeof(server));

    printf("Index Server running on port %d\n", port);

    while (1) {

        recvfrom(sd, &pdu, sizeof(pdu), 0, (struct sockaddr*)&client, &len);

        /* --------------------------- REGISTER (R) --------------------------- */
        if (pdu.type == 'R') {

            sscanf(pdu.data, "%s %s %s %d", peer, content, ip, &pnum);

            /* check conflicts */
            for (i = 0; i < count; i++) {
                if (!strcmp(table[i].peer, peer) &&
                    !strcmp(table[i].content, content)) {

                    struct PDU e;
                    e.type = 'E';
                    strcpy(e.data, "NameConflict");

                    sendto(sd, &e, sizeof(e), 0, (struct sockaddr*)&client, len);
                    goto done;
                }
            }

            /* add entry */
            strcpy(table[count].peer, peer);
            strcpy(table[count].content, content);
            strcpy(table[count].ip, ip);
            table[count].port = pnum;
            table[count].used = 0;
            count++;

            /* ACK */
            {
                struct PDU a;
                a.type = 'A';
                strcpy(a.data, "OK");
                sendto(sd, &a, sizeof(a), 0, (struct sockaddr*)&client, len);
            }
        }

        /* --------------------------- SEARCH (S) ----------------------------- */
        else if (pdu.type == 'S') {

            sscanf(pdu.data, "%s", content);

            best = -1;

            for (i = 0; i < count; i++) {
                if (!strcmp(table[i].content, content)) {

                    if (best == -1 || table[i].used < table[best].used)
                        best = i;
                }
            }

            if (best == -1) {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "NotFound");
                sendto(sd, &e, sizeof(e), 0, (struct sockaddr*)&client, len);
            } else {
                struct PDU s;
                char msg[100];

                sprintf(msg, "%s %d", table[best].ip, table[best].port);

                s.type = 'S';
                strcpy(s.data, msg);

                table[best].used++;

                sendto(sd, &s, sizeof(s), 0, (struct sockaddr*)&client, len);
            }
        }

        /* --------------------------- DEREGISTER (T) -------------------------- */
        else if (pdu.type == 'T') {

            sscanf(pdu.data, "%s %s", peer, content);

            for (i = 0; i < count; i++) {
                if (!strcmp(table[i].peer, peer) &&
                    !strcmp(table[i].content, content)) {

                    table[i] = table[count - 1];
                    count--;

                    {
                        struct PDU a;
                        a.type = 'A';
                        strcpy(a.data, "OK");
                        sendto(sd, &a, sizeof(a), 0, (struct sockaddr*)&client, len);
                    }

                    goto done;
                }
            }
        }

done:;
    }

    return 0;
}
