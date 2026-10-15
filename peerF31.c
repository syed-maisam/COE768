/* peer.c
 * Peer that talks to Index Server via UDP and transfers content via TCP using C/F PDUs.
 * One TCP listening socket per registered content (as required by lab).
 * C99 Standard Compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>

#define UDP_DATA_MAX 100
#define MAX_CONTENT  32   /* how many files this peer can serve */
#define DEFAULT_IDX_PORT 4000

struct pdu {
    char type;
    char data[UDP_DATA_MAX];
};

struct reg_item {
    char content[11];       /* <=10 + '\0' */
    int  sock;              /* TCP listening socket for this content */
    int  port;              /* TCP port assigned */
    int  in_use;
};

/* globals */
static int udp_sock = -1;
static struct sockaddr_in idx_addr;
static char my_peer[11] = "peer";     /* default peer name (<=10) */
static char my_ip[16]   = "127.0.0.1";

static struct reg_item regtab[MAX_CONTENT];

/* --- helpers: fixed field extraction/packing --- */

static void zero_data(char *dst) { 
    memset(dst, 0, UDP_DATA_MAX); 
}

static void put_fixed(char *data, int off, const char *src, int width) {
    int i;
    for (i = 0; i < width; ++i) data[off + i] = 0;
    for (i = 0; i < width && src[i]; ++i) data[off + i] = src[i];
}

static void get_fixed(char *dst, const char *data, int off, int width) {
    int i;
    for (i = 0; i < width; ++i) dst[i] = data[off + i];
    dst[width] = '\0';
    for (i = width - 1; i >= 0; --i) {
        if (dst[i] == '\0' || dst[i] == 0) dst[i] = '\0';
        else break;
    }
}

static int recv_all(int sd, void *buf, int want) {
    char *p = (char*)buf;
    int got = 0;
    while (got < want) {
        int r = (int)recv(sd, p + got, want - got, 0);
        if (r <= 0) return r;
        got += r;
    }
    return got;
}

static int send_all(int sd, const void *buf, int len) {
    const char *p = (const char*)buf;
    int sent = 0;
    while (sent < len) {
        int r = (int)send(sd, p + sent, len - sent, 0);
        if (r <= 0) return r;
        sent += r;
    }
    return sent;
}

/* Discover local IP */
static void determine_local_ip(void) {
    struct sockaddr_in tmp = idx_addr;
    struct sockaddr_in me; 
    socklen_t mlen = sizeof(me);
    const char *s;
    
    connect(udp_sock, (struct sockaddr*)&tmp, sizeof(tmp));
    if (getsockname(udp_sock, (struct sockaddr*)&me, &mlen) == 0) {
        s = inet_ntop(AF_INET, &me.sin_addr, my_ip, sizeof(my_ip));
        if (!s) strncpy(my_ip, "127.0.0.1", sizeof(my_ip));
    }
}

/* Create passive TCP listener */
static int make_tcp_listener(int *out_port) {
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    struct sockaddr_in a;
    socklen_t alen;
    
    if (sd < 0) { perror("tcp socket"); return -1; }
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(0);
    if (bind(sd, (struct sockaddr*)&a, sizeof(a)) < 0) { 
        perror("bind"); 
        close(sd); 
        return -1; 
    }
    if (listen(sd, 8) < 0) { 
        perror("listen"); 
        close(sd); 
        return -1; 
    }

    alen = sizeof(a);
    if (getsockname(sd, (struct sockaddr*)&a, &alen) < 0) { 
        perror("getsockname"); 
        close(sd); 
        return -1; 
    }
    *out_port = (int)ntohs(a.sin_port);
    return sd;
}

/* --- UDP request/response --- */

static int udp_send_pdu(char type, const char *payload) {
    struct pdu p; 
    int L;
    
    p.type = type;
    zero_data(p.data);
    if (payload) {
        L = (int)strlen(payload); 
        if (L > UDP_DATA_MAX) L = UDP_DATA_MAX;
        memcpy(p.data, payload, L);
    }
    return (int)sendto(udp_sock, &p, sizeof(p), 0, 
                       (struct sockaddr*)&idx_addr, sizeof(idx_addr));
}

static int udp_request_reply(struct pdu *req, struct pdu *resp) {
    struct sockaddr_in from; 
    socklen_t alen = sizeof(from);
    ssize_t n;
    
    if (sendto(udp_sock, req, sizeof(*req), 0, 
               (struct sockaddr*)&idx_addr, sizeof(idx_addr)) < 0) {
        perror("sendto"); 
        return -1;
    }
    n = recvfrom(udp_sock, resp, sizeof(*resp), 0, 
                 (struct sockaddr*)&from, &alen);
    if (n <= 0) return -1;
    return 0;
}

/* --- local registry --- */

static int reg_add(const char *content, int sock, int port) {
    int i;
    for (i = 0; i < MAX_CONTENT; ++i) {
        if (!regtab[i].in_use) {
            strncpy(regtab[i].content, content, 10);
            regtab[i].content[10] = '\0';
            regtab[i].sock = sock;
            regtab[i].port = port;
            regtab[i].in_use = 1;
            return 0;
        }
    }
    return -1;
}

static int reg_find(const char *content) {
    int i;
    for (i = 0; i < MAX_CONTENT; ++i) {
        if (regtab[i].in_use && strcmp(regtab[i].content, content) == 0) 
            return i;
    }
    return -1;
}

static void reg_remove(const char *content) {
    int i = reg_find(content);
    if (i >= 0) {
        close(regtab[i].sock);
        regtab[i].in_use = 0;
    }
}

/* --- TCP server: serve one client --- */

static void serve_one_client(int ns) {
    struct pdu req; 
    int r;
    char content[11];
    FILE *f;
    const char *msg;
    size_t mlen;
    
    memset(&req, 0, sizeof(req));
    r = recv_all(ns, &req, sizeof(req));
    if (r <= 0 || req.type != 'D') { 
        close(ns); 
        return; 
    }

    get_fixed(content, req.data, 0, 10);

    f = fopen(content, "rb");
    if (!f) {
        msg = "FILE_NOT_FOUND";
        mlen = strlen(msg);
        if (mlen > UDP_DATA_MAX - 1) mlen = UDP_DATA_MAX - 1;

        if (send_all(ns, "E", 1) > 0) {
            unsigned char len = (unsigned char)mlen;
            send_all(ns, &len, 1);
            send_all(ns, msg, (int)mlen);
        }
        close(ns);
        return;
    }

    for (;;) {
        unsigned char buf[UDP_DATA_MAX];
        size_t n = fread(buf, 1, UDP_DATA_MAX, f);
        int last = feof(f);
        char type = last ? 'F' : 'C';
        unsigned char len;

        if (send_all(ns, &type, 1) <= 0) break;

        len = (unsigned char)n;
        if (send_all(ns, &len, 1) <= 0) break;

        if (n > 0 && send_all(ns, buf, (int)n) <= 0) break;

        if (type == 'F') break;
    }

    fclose(f);
    shutdown(ns, SHUT_WR);
    close(ns);
}

/* --- Commands --- */

static void cmd_peername(const char *name) {
    if (!name || !name[0]) { 
        printf("usage: peername <name_up_to_10>\n"); 
        return; 
    }
    strncpy(my_peer, name, 10); 
    my_peer[10] = '\0';
    printf("[OK] peername set to '%s'\n", my_peer);
}

static void cmd_register(const char *content) {
    int port = 0;
    int sd = make_tcp_listener(&port);
    struct pdu req, resp; 
    char addr[80];
    struct pdu t;
    
    if (sd < 0) { 
        printf("[ERR] cannot create TCP listener\n"); 
        return; 
    }

    req.type = 'R'; 
    zero_data(req.data);
    put_fixed(req.data, 0,  my_peer,   10);
    put_fixed(req.data, 10, content,   10);
    snprintf(addr, sizeof(addr), "%s:%d", my_ip, port);
    memcpy(req.data + 20, addr, strlen(addr));

    if (udp_request_reply(&req, &resp) == 0 && resp.type == 'A') {
        if (reg_add(content, sd, port) == 0) {
            printf("[OK] registered '%s' at %s:%d\n", content, my_ip, port);
        } else {
            printf("[ERR] local registry full; deregistering\n");
            close(sd);
            t.type = 'T'; 
            zero_data(t.data);
            put_fixed(t.data, 0, my_peer, 10);
            put_fixed(t.data, 10, content, 10);
            udp_send_pdu('T', NULL);
        }
    } else {
        printf("[ERR] index rejected register\n");
        close(sd);
    }
}

static void cmd_search(const char *content, char *out_ip, int out_ip_sz, int *out_port) {
    struct pdu req, resp; 
    char addr[UDP_DATA_MAX+1];
    char *colon;
    
    req.type = 'S'; 
    zero_data(req.data);
    put_fixed(req.data, 0, my_peer, 10);
    put_fixed(req.data, 10, content, 10);

    if (udp_request_reply(&req, &resp) == 0) {
        if (resp.type == 'S') {
            memcpy(addr, resp.data, UDP_DATA_MAX);
            addr[UDP_DATA_MAX] = '\0';
            colon = strchr(addr, ':'); 
            if (!colon) { 
                printf("[ERR] bad addr\n"); 
                return; 
            }
            *colon = '\0';
            strncpy(out_ip, addr, out_ip_sz-1);
            out_ip[out_ip_sz-1] = '\0';
            *out_port = atoi(colon + 1);
            printf("[FOUND] %s:%d\n", out_ip, *out_port);
        } else if (resp.type == 'E') {
            printf("[ERR] %s\n", resp.data);
        } else {
            printf("[ERR] unexpected reply\n");
        }
    } else {
        printf("[ERR] no response from index\n");
    }
}

static void cmd_download(const char *content) {
    char sip[32]; 
    int sport = 0;
    int sd;
    struct sockaddr_in a;
    struct pdu d;
    FILE *f;
    int idx;
    
    cmd_search(content, sip, sizeof(sip), &sport);
    if (sport <= 0) return;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) { 
        perror("tcp socket"); 
        return; 
    }

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((uint16_t)sport);
    inet_pton(AF_INET, sip, &a.sin_addr);

    if (connect(sd, (struct sockaddr*)&a, sizeof(a)) < 0) { 
        perror("connect"); 
        close(sd); 
        return; 
    }

    d.type = 'D'; 
    zero_data(d.data);
    put_fixed(d.data, 0, content, 10);
    if (send_all(sd, &d, sizeof(d)) <= 0) { 
        printf("[ERR] send D\n"); 
        close(sd); 
        return; 
    }

    f = fopen(content, "wb");
    if (!f) { 
        perror("fopen"); 
        close(sd); 
        return; 
    }

    for (;;) {
        char type;
        int r = recv_all(sd, &type, 1);
        unsigned char len = 0;

        if (r <= 0) { 
            printf("[ERR] recv type\n"); 
            break; 
        }

        if (type == 'E') {
            if (recv_all(sd, &len, 1) <= 0) break;
            if (len > 0) {
                unsigned char msgbuf[UDP_DATA_MAX];
                if (recv_all(sd, msgbuf, len) > 0) {
                    fwrite(msgbuf, 1, len, stdout);
                    printf("\n");
                }
            }
            break;
        }

        if (recv_all(sd, &len, 1) <= 0) break;

        if (len > 0) {
            unsigned char buf[UDP_DATA_MAX];
            if (recv_all(sd, buf, len) <= 0) break;
            fwrite(buf, 1, len, f);
        }

        if (type == 'F') break;
    }

    fclose(f);
    close(sd);
    printf("[OK] downloaded '%s'\n", content);
    cmd_register(content);
    
    /* add new TCP listener after auto-register */
    idx = reg_find(content);
    if (idx >= 0) {
        /* Note: caller should handle FD_SET in main loop */
    }
}

static void cmd_deregister(const char *content) {
    struct pdu t, resp; 
    
    t.type = 'T'; 
    zero_data(t.data);
    put_fixed(t.data, 0, my_peer, 10);
    put_fixed(t.data, 10, content, 10);
    if (udp_request_reply(&t, &resp) == 0 && resp.type == 'A') {
        reg_remove(content);
        printf("[OK] deregistered '%s'\n", content);
    } else {
        printf("[ERR] deregister failed\n");
    }
}

static void cmd_list(void) {
    struct pdu o, resp; 
    
    o.type = 'O'; 
    zero_data(o.data);
    if (udp_request_reply(&o, &resp) == 0 && resp.type == 'O') {
        printf("[LIST] %s\n", resp.data);
    } else {
        printf("[ERR] list failed\n");
    }
}

static void print_help(void) {
    printf("Commands:\n");
    printf("  peername <name_up_to_10>\n");
    printf("  register <content_name>\n");
    printf("  search <content_name>\n");
    printf("  download <content_name>\n");
    printf("  deregister <content_name>\n");
    printf("  list\n");
    printf("  quit\n");
}

int main(int argc, char **argv) {
    const char *idx_ip;
    int idx_port;
    fd_set afds, rfds;
    int fdmax;
    int i;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <index_ip> <index_udp_port>\n", argv[0]);
        return 1;
    }
    idx_ip = argv[1];
    idx_port = atoi(argv[2]);
    if (idx_port <= 0) idx_port = DEFAULT_IDX_PORT;

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { 
        perror("udp socket"); 
        return 1; 
    }
    memset(&idx_addr, 0, sizeof(idx_addr));
    idx_addr.sin_family = AF_INET;
    idx_addr.sin_port = htons((uint16_t)idx_port);
    inet_pton(AF_INET, idx_ip, &idx_addr.sin_addr);

    determine_local_ip();
    printf("[INFO] local IP assumed: %s\n", my_ip);
    print_help();

    FD_ZERO(&afds);
    FD_SET(0, &afds);
    FD_SET(udp_sock, &afds);
    fdmax = udp_sock;

    for (i = 0; i < MAX_CONTENT; ++i) 
        regtab[i].in_use = 0;

    while (1) {
        rfds = afds;
        if (select(fdmax + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select"); 
            break;
        }

        /* stdin */
        if (FD_ISSET(0, &rfds)) {
            char line[256]; 
            char *tok;
            size_t L;
            
            if (!fgets(line, sizeof(line), stdin)) 
                break;
            L = strlen(line); 
            if (L && line[L-1] == '\n') 
                line[L-1] = '\0';

            tok = strtok(line, " ");
            if (!tok) continue;

            if (strcmp(tok, "peername") == 0) {
                char *nm = strtok(NULL, " ");
                cmd_peername(nm);

            } else if (strcmp(tok, "register") == 0) {
                char *cn = strtok(NULL, " ");
                if (!cn) {
                    printf("usage: register <content_name>\n");
                } else {
                    FILE *fp = fopen(cn, "rb");
                    if (fp) {
                        int idx;
                        fclose(fp);
                        cmd_register(cn);

                        idx = reg_find(cn);
                        if (idx >= 0) {
                            FD_SET(regtab[idx].sock, &afds);
                            if (regtab[idx].sock > fdmax) 
                                fdmax = regtab[idx].sock;
                        }
                    } else {
                        printf("[ERROR] Cannot register '%s', file not found.\n", cn);
                    }
                }

            } else if (strcmp(tok, "download") == 0) {
                char *cn = strtok(NULL, " ");

                if (!cn) {
                    printf("usage: download <content_name>\n");
                } else if (access(cn, F_OK) == 0) {
                    printf("File: %s already exists on this peer, try a different file please.\n", cn);
                } else {
                    int idx;
                    cmd_download(cn);

                    /* add new TCP listener after auto-register */
                    idx = reg_find(cn);
                    if (idx >= 0) {
                        FD_SET(regtab[idx].sock, &afds);
                        if (regtab[idx].sock > fdmax) 
                            fdmax = regtab[idx].sock;
                    }
                }

            } else if (strcmp(tok, "search") == 0) {
                char *cn = strtok(NULL, " ");
                char ip[32]; 
                int prt = 0;
                cmd_search(cn, ip, sizeof(ip), &prt);

            } else if (strcmp(tok, "deregister") == 0) {
                char *cn = strtok(NULL, " ");
                int idx = reg_find(cn);
                if (idx >= 0) {
                    FD_CLR(regtab[idx].sock, &afds);
                }
                cmd_deregister(cn);

            } else if (strcmp(tok, "list") == 0) {
                cmd_list();

            } else if (strcmp(tok, "quit") == 0) {
                for (i = 0; i < MAX_CONTENT; ++i) {
                    if (regtab[i].in_use) {
                        struct pdu t, resp; 
                        t.type = 'T'; 
                        zero_data(t.data);
                        put_fixed(t.data, 0, my_peer, 10);
                        put_fixed(t.data, 10, regtab[i].content, 10);
                        udp_request_reply(&t, &resp);
                        close(regtab[i].sock);
                        regtab[i].in_use = 0;
                    }
                }
                printf("Bye.\n");
                break;

            } else {
                print_help();
            }
        }

        /* UDP unsolicited */
        if (FD_ISSET(udp_sock, &rfds)) {
            struct pdu rp; 
            struct sockaddr_in from; 
            socklen_t alen = sizeof(from);
            ssize_t n = recvfrom(udp_sock, &rp, sizeof(rp), 0, 
                                 (struct sockaddr*)&from, &alen);
            if (n > 0) {
                printf("[IDX unsolicited] %c %.*s\n", rp.type, 
                       UDP_DATA_MAX, rp.data);
            }
        }

        /* TCP listeners */
        for (i = 0; i < MAX_CONTENT; ++i) {
            if (regtab[i].in_use && FD_ISSET(regtab[i].sock, &rfds)) {
                struct sockaddr_in cli; 
                socklen_t clen = sizeof(cli);
                int ns = accept(regtab[i].sock, (struct sockaddr*)&cli, &clen);
                if (ns >= 0) 
                    serve_one_client(ns);
            }
        }
    }

    close(udp_sock);
    for (i = 0; i < MAX_CONTENT; ++i)
        if (regtab[i].in_use) 
            close(regtab[i].sock);

    return 0;
}
