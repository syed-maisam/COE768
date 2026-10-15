#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_DATA 100

/* Simple PDU definition */
struct PDU {
    char type;
    char data[MAX_DATA];
};

/* Registration table entry */
typedef struct {
    char peer[10];     /* peer name (max 9 chars + '\0') */
    char content[10];  /* content name (max 9 chars + '\0') */
    char ip[16];       /* IPv4 string, e.g. "127.0.0.1" */
    int  port;         /* TCP port of content server */
    int  used;         /* download count for load-balancing */
} entry;

entry table[100];
int count = 0;

int main(int argc, char *argv[]) {
    int sd;
    int port;
    struct sockaddr_in server, client;
    socklen_t len;
    struct PDU pdu;

    int i, best;
    char peer[10], content[10], ip[16];
    int pnum;

    if (argc != 2) {
        printf("Usage: %s <udp_port>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port        = htons(port);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        close(sd);
        return 1;
    }

    printf("Index server running on UDP port %d\n", port);

    len = sizeof(client);

    while (1) {
        memset(&pdu, 0, sizeof(pdu));
        if (recvfrom(sd, &pdu, sizeof(pdu), 0,
                     (struct sockaddr *)&client, &len) < 0) {
            perror("recvfrom");
            continue;
        }

        /* ====================== REGISTER (R) ====================== */
        if (pdu.type == 'R') {
            /* data format: peer content ip port */
            if (sscanf(pdu.data, "%9s %9s %15s %d",
                       peer, content, ip, &pnum) != 4) {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "BadRegister");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
                goto done;
            }

            /* check for peer+content conflict */
            for (i = 0; i < count; i++) {
                if (strcmp(table[i].peer, peer) == 0 &&
                    strcmp(table[i].content, content) == 0) {

                    struct PDU e;
                    e.type = 'E';
                    strcpy(e.data, "NameConflict");
                    sendto(sd, &e, sizeof(e), 0,
                           (struct sockaddr *)&client, len);
                    goto done;
                }
            }

            /* add new entry */
            if (count < 100) {
                strcpy(table[count].peer,    peer);
                strcpy(table[count].content, content);
                strcpy(table[count].ip,      ip);
                table[count].port = pnum;
                table[count].used = 0;
                count++;

                /* ACK */
                {
                    struct PDU a;
                    a.type = 'A';
                    strcpy(a.data, "OK");
                    sendto(sd, &a, sizeof(a), 0,
                           (struct sockaddr *)&client, len);
                }
            } else {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "TableFull");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
            }
        }

        /* ====================== SEARCH (S) ======================== */
        else if (pdu.type == 'S') {
            /* data format: content */
            if (sscanf(pdu.data, "%9s", content) != 1) {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "BadSearch");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
                goto done;
            }

            best = -1;
            for (i = 0; i < count; i++) {
                if (strcmp(table[i].content, content) == 0) {
                    if (best == -1 || table[i].used < table[best].used) {
                        best = i;
                    }
                }
            }

            if (best == -1) {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "NotFound");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
            } else {
                struct PDU s;
                char msg[MAX_DATA];

                snprintf(msg, sizeof(msg), "%s %d",
                         table[best].ip, table[best].port);

                s.type = 'S';
                strncpy(s.data, msg, MAX_DATA - 1);
                s.data[MAX_DATA - 1] = '\0';

                table[best].used++;

                sendto(sd, &s, sizeof(s), 0,
                       (struct sockaddr *)&client, len);
            }
        }

        /* =================== DE-REGISTER (T) ====================== */
        else if (pdu.type == 'T') {
            /* data format: peer content */
            if (sscanf(pdu.data, "%9s %9s", peer, content) != 2) {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "BadDereg");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
                goto done;
            }

            for (i = 0; i < count; i++) {
                if (strcmp(table[i].peer, peer) == 0 &&
                    strcmp(table[i].content, content) == 0) {

                    /* replace with last */
                    table[i] = table[count - 1];
                    count--;

                    {
                        struct PDU a;
                        a.type = 'A';
                        strcpy(a.data, "OK");
                        sendto(sd, &a, sizeof(a), 0,
                               (struct sockaddr *)&client, len);
                    }
                    goto done;
                }
            }

            /* not found - silently ignore or send error */
            {
                struct PDU e;
                e.type = 'E';
                strcpy(e.data, "NotRegistered");
                sendto(sd, &e, sizeof(e), 0,
                       (struct sockaddr *)&client, len);
            }
        }

        /* =================== LIST CONTENT (O) ====================== */
        else if (pdu.type == 'O') {
            struct PDU o;
            char buffer[MAX_DATA];
            int pos;
            int name_len;

            o.type = 'O';
            buffer[0] = '\0';
            pos = 0;

            for (i = 0; i < count; i++) {
                name_len = (int)strlen(table[i].content);

                /* +1 for space or '\0' */
                if (pos + name_len + 1 >= MAX_DATA) {
                    break;
                }

                if (pos > 0) {
                    buffer[pos] = ' ';
                    pos++;
                }

                memcpy(buffer + pos, table[i].content, name_len);
                pos += name_len;
                buffer[pos] = '\0';
            }

            if (pos == 0) {
                strcpy(buffer, "EMPTY");
            }

            strncpy(o.data, buffer, MAX_DATA - 1);
            o.data[MAX_DATA - 1] = '\0';

            sendto(sd, &o, sizeof(o), 0,
                   (struct sockaddr *)&client, len);
        }

        else {
            /* unknown PDU type, ignore or send error */
            struct PDU e;
            e.type = 'E';
            strcpy(e.data, "UnknownType");
            sendto(sd, &e, sizeof(e), 0,
                   (struct sockaddr *)&client, len);
        }

done:
        ;
    }

    close(sd);
    return 0;
}
