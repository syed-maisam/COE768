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

/* Track which contents this peer has registered (for quit/deregister) */
#define MAX_MY_CONTENT 50
char my_contents[MAX_MY_CONTENT][10];
int  my_count = 0;

/* -------------------- Helpers to track our content -------------------- */
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

/* ====================== CONTENT SERVER (CHILD) ====================== */
/* Serve one file on a given TCP port, forever */
void serve_content(const char *filename, int port) {
    int sd, newsd;
    struct sockaddr_in server, client;
    socklen_t len;
    FILE *fp;
    struct PDU out;
    int n;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("[SERVER] socket");
        exit(1);
    }

    memset(&server, 0, sizeof(server));
    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port        = htons(port);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("[SERVER] bind");
        close(sd);
        exit(1);
    }

    if (listen(sd, 5) < 0) {
        perror("[SERVER] listen");
        close(sd);
        exit(1);
    }

    printf("[SERVER] Serving '%s' on TCP port %d\n", filename, port);

    while (1) {
        len = sizeof(client);
        newsd = accept(sd, (struct sockaddr *)&client, &len);
        if (newsd < 0) {
            perror("[SERVER] accept");
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
            /* You could check request.data vs filename if you want */
        }

        fp = fopen(filename, "rb");
        if (fp == NULL) {
            perror("[SERVER] fopen (cannot open file to send)");
            close(newsd);
            continue;
        }

        /* Send file in C-type PDUs: type + up to MAX_DATA bytes of data */
        while (1) {
            n = (int)fread(out.data, 1, MAX_DATA, fp);
            if (n <= 0) break;

            out.type = 'C';
            /* send exactly 1 + n bytes (1 for type, n for data) */
            if (send(newsd, &out, 1 + n, 0) < 0) {
                perror("[SERVER] send");
                break;
            }
        }

        fclose(fp);
        close(newsd);
    }

    close(sd);
    exit(0);
}

/* ====================== CONTENT CLIENT (DOWNLOAD) ====================== */
/* Returns number of bytes written to file (0 or negative -> failed) */
long download_file(const char *ip, int port, const char *filename) {
    int sd;
    struct sockaddr_in server;
    FILE *fp;
    struct PDU p;
    int n;
    long total = 0;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        perror("[CLIENT] socket");
        return -1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        perror("[CLIENT] inet_pton");
        close(sd);
        return -1;
    }

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("[CLIENT] connect");
        close(sd);
        return -1;
    }

    /* send D request */
    {
        struct PDU d;
        d.type = 'D';
        strncpy(d.data, filename, MAX_DATA - 1);
        d.data[MAX_DATA - 1] = '\0';

        if (send(sd, &d, sizeof(d), 0) < 0) {
            perror("[CLIENT] send D");
            close(sd);
            return -1;
        }
    }

    fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("[CLIENT] fopen (cannot create local file)");
        close(sd);
        return -1;
    }

    while (1) {
        n = recv(sd, &p, sizeof(p), 0);
        if (n <= 0) break;

        if (p.type != 'C') {
            /* ignore any non-C packets */
            continue;
        }

        /* n includes 1 byte for type, so n-1 is actual data size */
        fwrite(p.data, 1, n - 1, fp);
        total += (n - 1);
    }

    fclose(fp);
    close(sd);

    if (total > 0) {
        printf("[CLIENT] Download of '%s' completed (%ld bytes).\n",
               filename, total);
    } else {
        printf("[CLIENT] Download of '%s' failed or empty.\n", filename);
    }

    return total;
}

/* ============================ MAIN PEER ============================ */
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
        long bytes;
        char ipbuf[16];
        int cport;

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

        /* -------------------------- register -------------------------- */
        if (strncmp(cmd, "register ", 9) == 0) {
            if (sscanf(cmd + 9, "%63s", arg) != 1) {
                printf("Usage: register <filename>\n");
                continue;
            }

            /* quick check that file exists BEFORE forking */
            {
                FILE *test = fopen(arg, "rb");
                if (test == NULL) {
                    printf("[REGISTER] File '%s' does not exist in this folder.\n", arg);
                    continue;
                }
                fclose(test);
            }

            /* choose a random port for this content server */
            port = 10000 + rand() % 50000;

            /* fork a child to serve this content */
            if (fork() == 0) {
                /* child process: act as content server */
                serve_content(arg, port);
                exit(0);
            }

            /* parent: send R to index */
            out.type = 'R';
            {
                char msg[MAX_DATA];
                char ip[16] = "127.0.0.1";
                sprintf(msg, "%s %s %s %d", peername, arg, ip, port);
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
                printf("[REGISTER] Registered '%s' successfully.\n", arg);
                add_my_content(arg);
            } else {
                printf("[REGISTER] Error: %s\n", in.data);
            }
        }

        /* --------------------------- search --------------------------- */
        else if (strncmp(cmd, "search ", 7) == 0) {
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
                printf("[SEARCH] Error: %s\n", in.data);
            } else if (in.type == 'S') {
                if (sscanf(in.data, "%15s %d", ipbuf, &cport) != 2) {
                    printf("[SEARCH] Malformed reply.\n");
                } else {
                    printf("[SEARCH] Downloading '%s' from %s:%d ...\n",
                           arg, ipbuf, cport);
                    bytes = download_file(ipbuf, cport, arg);

                    /* Only auto-register if we actually received data */
                    if (bytes > 0) {
                        printf("[SEARCH] Auto-registering downloaded content '%s'...\n", arg);

                        port = 10000 + rand() % 50000;
                        if (fork() == 0) {
                            serve_content(arg, port);
                            exit(0);
                        }

                        out.type = 'R';
                        {
                            char msg[MAX_DATA];
                            char ip2[16] = "127.0.0.1";
                            sprintf(msg, "%s %s %s %d", peername, arg, ip2, port);
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
                            printf("[SEARCH] Auto-registration of '%s' successful.\n", arg);
                            add_my_content(arg);
                        } else {
                            printf("[SEARCH] Auto-register error: %s\n", in.data);
                        }
                    } else {
                        printf("[SEARCH] Download failed or empty, not auto-registering.\n");
                    }
                }
            } else {
                printf("[SEARCH] Unexpected reply type %c\n", in.type);
            }
        }

        /* ---------------------------- list ---------------------------- */
        else if (strcmp(cmd, "list") == 0) {
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
                printf("[LIST] Registered contents: %s\n", in.data);
            } else {
                printf("[LIST] Unexpected reply type %c\n", in.type);
            }
        }

        /* ------------------------- deregister ------------------------- */
        else if (strncmp(cmd, "deregister ", 11) == 0) {
            if (sscanf(cmd + 11, "%63s", arg) != 1) {
                printf("Usage: deregister <filename>\n");
                continue;
            }

            out.type = 'T';
            {
                char msg[MAX_DATA];
                sprintf(msg, "%s %s", peername, arg);
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
                printf("[DEREGISTER] '%s' de-registered.\n", arg);
                remove_my_content(arg);
            } else {
                printf("[DEREGISTER] Error: %s\n", in.data);
            }
        }

        /* ----------------------------- quit --------------------------- */
        else if (strcmp(cmd, "quit") == 0) {
            int i;
            for (i = 0; i < my_count; i++) {
                out.type = 'T';
                {
                    char msg[MAX_DATA];
                    sprintf(msg, "%s %s", peername, my_contents[i]);
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

            printf("[QUIT] All contents de-registered. Exiting.\n");
            break;
        }

        else if (cmd[0] != '\0') {
            printf("Unknown command. Use: register, search, list, deregister, quit\n");
        }
    }

    close(udp);
    return 0;
}
