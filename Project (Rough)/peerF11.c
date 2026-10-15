#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_DATA 100

/* Simple PDU definition */
struct PDU {
    char type;
    char data[MAX_DATA];
};

/* List of contents registered by this peer (for deregister/quit) */
#define MAX_MY_CONTENT 50

char my_contents[MAX_MY_CONTENT][10];
int  my_count = 0;

/* Add a content name to local list (no duplicates) */
void add_my_content(const char *name) {
    int i;
    if (my_count >= MAX_MY_CONTENT) return;

    for (i = 0; i < my_count; i++) {
        if (strcmp(my_contents[i], name) == 0)
            return;
    }
    strncpy(my_contents[my_count], name, 9);
    my_contents[my_count][9] = '\0';
    my_count++;
}

/* Remove a content name from local list */
void remove_my_content(const char *name) {
    int i;
    for (i = 0; i < my_count; i++) {
        if (strcmp(my_contents[i], name) == 0) {
            if (i != my_count - 1) {
                strcpy(my_contents[i], my_contents[my_count - 1]);
            }
            my_count--;
            return;
        }
    }
}

/* ====================== CONTENT SERVER ====================== */
/* Child process: serves one file on a given TCP port */
void serve_content(const char *filename, int port) {
    int sd, newsd;
    struct sockaddr_in server, client;
    socklen_t len;
    FILE *fp;
    struct PDU out;
    int n;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&server, 0, sizeof(server));
    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port        = htons(port);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        close(sd);
        exit(1);
    }

    if (listen(sd, 5) < 0) {
        perror("listen");
        close(sd);
        exit(1);
    }

    printf("[SERVER] Serving '%s' on TCP port %d\n", filename, port);

    while (1) {
        len = sizeof(client);
        newsd = accept(sd, (struct sockaddr *)&client, &len);
        if (newsd < 0) {
            perror("accept");
            continue;
        }

        /* Expect a D-type PDU with the filename */
        {
            struct PDU request;
            n = recv(newsd, &request, sizeof(request), 0);
            if (n <= 0) {
                close(newsd);
                continue;
            }

            if (request.type != 'D') {
                close(newsd);
                continue;
            }
        }

        fp = fopen(filename, "rb");
        if (fp == NULL) {
            perror("fopen");
            close(newsd);
            continue;
        }

        /* Send file in C-type PDUs */
        while (1) {
            n = (int)fread(out.data, 1, MAX_DATA, fp);
            if (n <= 0) break;

            out.type = 'C';
            /* send 1 byte for type + n bytes of data */
            if (send(newsd, &out, 1 + n, 0) < 0) {
                perror("send");
                break;
            }
        }

        fclose(fp);
        close(newsd);
    }

    /* Not reached */
    close(sd);
    exit(0);
}

/* ====================== CONTENT CLIENT ====================== */
/* Downloads a file from given IP/port using D and C-type PDUs */
void download_file(const char *ip, int port, const char *filename) {
    int sd;
    struct sockaddr_in server;
    FILE *fp;
    struct PDU p;
    int n;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("socket");
        return;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        perror("inet_pton");
        close(sd);
        return;
    }

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sd);
        return;
    }

    /* send D request */
    {
        struct PDU d;
        d.type = 'D';
        strncpy(d.data, filename, MAX_DATA - 1);
        d.data[MAX_DATA - 1] = '\0';

        if (send(sd, &d, sizeof(d), 0) < 0) {
            perror("send");
            close(sd);
            return;
        }
    }

    fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("fopen");
        close(sd);
        return;
    }

    while (1) {
        n = recv(sd, &p, sizeof(p), 0);
        if (n <= 0) break;

        if (p.type != 'C') {
            /* ignore any non-C packets */
            continue;
        }

        /* n includes 1 byte for type, remainder is data */
        fwrite(p.data, 1, n - 1, fp);
    }

    fclose(fp);
    close(sd);

    printf("Download of '%s' completed.\n", filename);
}

/* ============================ MAIN =========================== */
int main(int argc, char *argv[]) {
    int udp;
    struct sockaddr_in index_addr;
    char peername[10];
    char cmd[128];

    if (argc != 4) {
        printf("Usage: %s <peername> <index_ip> <index_port>\n", argv[0]);
        return 1;
    }

    strncpy(peername, argv[1], 9);
    peername[9] = '\0';

    udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp < 0) {
        perror("socket");
        return 1;
    }

    memset(&index_addr, 0, sizeof(index_addr));
    index_addr.sin_family = AF_INET;
    index_addr.sin_port   = htons(atoi(argv[3]));
    if (inet_pton(AF_INET, argv[2], &index_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(udp);
        return 1;
    }

    srand((unsigned int)time(NULL));

    printf("Peer '%s' started. Connected to index %s:%s\n",
           peername, argv[2], argv[3]);
    printf("Commands: register <file>, search <file>, list, deregister <file>, quit\n");

    while (1) {
        struct PDU out, in;
        socklen_t len;
        char arg[64];
        int port;
        int n;

        printf("> ");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break;
        }

        /* strip newline */
        n = (int)strlen(cmd);
        if (n > 0 && cmd[n - 1] == '\n') {
            cmd[n - 1] = '\0';
        }

        if (strncmp(cmd, "register ", 9) == 0) {
            /* register <filename> */
            if (sscanf(cmd + 9, "%63s", arg) != 1) {
                printf("Usage: register <filename>\n");
                continue;
            }

            /* choose a random port for this content server */
            port = 10000 + rand() % 50000;

            /* fork a child to serve this content */
            if (fork() == 0) {
                /* child */
                serve_content(arg, port);
                exit(0);
            }

            /* parent: send R to index */
            out.type = 'R';
            {
                char msg[MAX_DATA];
                char ip[16] = "127.0.0.1";

                snprintf(msg, sizeof(msg), "%s %s %s %d",
                         peername, arg, ip, port);
                strncpy(out.data, msg, MAX_DATA - 1);
                out.data[MAX_DATA - 1] = '\0';
            }

            if (sendto(udp, &out, sizeof(out), 0,
                       (struct sockaddr *)&index_addr, sizeof(index_addr)) < 0) {
                perror("sendto");
                continue;
            }

            len = sizeof(index_addr);
            if (recvfrom(udp, &in, sizeof(in), 0,
                         (struct sockaddr *)&index_addr, &len) < 0) {
                perror("recvfrom");
                continue;
            }

            if (in.type == 'A') {
                printf("Registered '%s' successfully.\n", arg);
                add_my_content(arg);
            } else {
                printf("Register error: %s\n", in.data);
            }
        }

        else if (strncmp(cmd, "search ", 7) == 0) {
            /* search <filename> */
            char ip[16];
            int cport;

            if (sscanf(cmd + 7, "%63s", arg) != 1) {
                printf("Usage: search <filename>\n");
                continue;
            }

            out.type = 'S';
            strncpy(out.data, arg, MAX_DATA - 1);
            out.data[MAX_DATA - 1] = '\0';

            if (sendto(udp, &out, sizeof(out), 0,
                       (struct sockaddr *)&index_addr, sizeof(index_addr)) < 0) {
                perror("sendto");
                continue;
            }

            len = sizeof(index_addr);
            if (recvfrom(udp, &in, sizeof(in), 0,
                         (struct sockaddr *)&index_addr, &len) < 0) {
                perror("recvfrom");
                continue;
            }

            if (in.type == 'E') {
                printf("Search error: %s\n", in.data);
            } else if (in.type == 'S') {
                if (sscanf(in.data, "%15s %d", ip, &cport) != 2) {
                    printf("Malformed search reply.\n");
                } else {
                    printf("Downloading '%s' from %s:%d ...\n",
                           arg, ip, cport);
                    download_file(ip, cport, arg);

                    /* After download, automatically register as server */
                    printf("Auto-registering downloaded content '%s'...\n", arg);

                    /* choose new port for our server of this content */
                    port = 10000 + rand() % 50000;

                    if (fork() == 0) {
                        serve_content(arg, port);
                        exit(0);
                    }

                    out.type = 'R';
                    {
                        char msg[MAX_DATA];
                        char ip2[16] = "127.0.0.1";

                        snprintf(msg, sizeof(msg), "%s %s %s %d",
                                 peername, arg, ip2, port);
                        strncpy(out.data, msg, MAX_DATA - 1);
                        out.data[MAX_DATA - 1] = '\0';
                    }

                    if (sendto(udp, &out, sizeof(out), 0,
                               (struct sockaddr *)&index_addr,
                               sizeof(index_addr)) < 0) {
                        perror("sendto");
                        continue;
                    }

                    len = sizeof(index_addr);
                    if (recvfrom(udp, &in, sizeof(in), 0,
                                 (struct sockaddr *)&index_addr, &len) < 0) {
                        perror("recvfrom");
                        continue;
                    }

                    if (in.type == 'A') {
                        printf("Auto-registration of '%s' successful.\n", arg);
                        add_my_content(arg);
                    } else {
                        printf("Auto-register error: %s\n", in.data);
                    }
                }
            } else {
                printf("Unexpected reply type %c\n", in.type);
            }
        }

        else if (strcmp(cmd, "list") == 0) {
            /* O-type list request */
            out.type = 'O';
            out.data[0] = '\0';

            if (sendto(udp, &out, sizeof(out), 0,
                       (struct sockaddr *)&index_addr, sizeof(index_addr)) < 0) {
                perror("sendto");
                continue;
            }

            len = sizeof(index_addr);
            if (recvfrom(udp, &in, sizeof(in), 0,
                         (struct sockaddr *)&index_addr, &len) < 0) {
                perror("recvfrom");
                continue;
            }

            if (in.type == 'O') {
                printf("Registered contents: %s\n", in.data);
            } else {
                printf("Unexpected reply to list: %c\n", in.type);
            }
        }

        else if (strncmp(cmd, "deregister ", 11) == 0) {
            /* deregister <filename> */
            if (sscanf(cmd + 11, "%63s", arg) != 1) {
                printf("Usage: deregister <filename>\n");
                continue;
            }

            out.type = 'T';
            {
                char msg[MAX_DATA];
                snprintf(msg, sizeof(msg), "%s %s", peername, arg);
                strncpy(out.data, msg, MAX_DATA - 1);
                out.data[MAX_DATA - 1] = '\0';
            }

            if (sendto(udp, &out, sizeof(out), 0,
                       (struct sockaddr *)&index_addr, sizeof(index_addr)) < 0) {
                perror("sendto");
                continue;
            }

            len = sizeof(index_addr);
            if (recvfrom(udp, &in, sizeof(in), 0,
                         (struct sockaddr *)&index_addr, &len) < 0) {
                perror("recvfrom");
                continue;
            }

            if (in.type == 'A') {
                printf("Deregistered '%s'.\n", arg);
                remove_my_content(arg);
            } else {
                printf("Deregister error: %s\n", in.data);
            }
        }

        else if (strcmp(cmd, "quit") == 0) {
            int i;
            /* De-register all our contents */
            for (i = 0; i < my_count; i++) {
                out.type = 'T';
                {
                    char msg[MAX_DATA];
                    snprintf(msg, sizeof(msg), "%s %s",
                             peername, my_contents[i]);
                    strncpy(out.data, msg, MAX_DATA - 1);
                    out.data[MAX_DATA - 1] = '\0';
                }

                if (sendto(udp, &out, sizeof(out), 0,
                           (struct sockaddr *)&index_addr,
                           sizeof(index_addr)) < 0) {
                    perror("sendto");
                    continue;
                }

                len = sizeof(index_addr);
                if (recvfrom(udp, &in, sizeof(in), 0,
                             (struct sockaddr *)&index_addr, &len) < 0) {
                    perror("recvfrom");
                    continue;
                }
            }

            printf("All contents de-registered. Exiting.\n");
            break;
        }

        else if (cmd[0] != '\0') {
            printf("Unknown command. Use: register, search, list, deregister, quit\n");
        }
    }

    close(udp);
    return 0;
}
