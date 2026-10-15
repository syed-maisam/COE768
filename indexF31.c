/* index_server.c
 * UDP Index Server implementing: R (register), S (search), T (deregister), O (list)
 * Replies with: A (ack), E (error), S (address "IP:PORT"), O (list)
 * Load-balances S by returning the least-used server for a given content.
 * C99 Standard Compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_ENTRIES 75
#define UDP_DATA_MAX 100
#define DEFAULT_PORT 4000

/* PDU: 1 byte Type + up to 100 bytes Data (meaning depends on Type) */
struct pdu {
    char type;
    char data[UDP_DATA_MAX];
};

/* One registry row */
struct entry {
    char peer[11];       /* peer name (<=10) + '\0' */
    char content[11];    /* content name (<=10) + '\0' */
    char ip[16];         /* dotted quad "a.b.c.d" */
    int  port;           /* TCP port */
    int  used;           /* times returned on S for load-balancing */
    int  in_use;         /* 0/1 */
};

static struct entry table[MAX_ENTRIES];

static void trim_fix(char *dst, const char *src, int max) {
    /* copy up to max chars and ensure null-terminated */
    int n = 0;
    while (n < max && src[n]) { 
        dst[n] = src[n]; 
        n++; 
    }
    dst[n] = '\0';
}

static void extract_fixed(char *dst, const char *data, int offset, int width) {
    /* copy a fixed-width (possibly zero-padded) field and ensure null */
    int i;
    for (i = 0; i < width; ++i) 
        dst[i] = data[offset + i];
    dst[width] = '\0';
    /* strip trailing zeros */
    for (i = width - 1; i >= 0; --i) {
        if (dst[i] == '\0' || dst[i] == 0) 
            dst[i] = '\0';
        else 
            break;
    }
}

static void send_pdu(int s, struct sockaddr_in *to, socklen_t alen, 
                     char type, const char *msg) {
    struct pdu r; 
    int L;
    
    r.type = type;
    memset(r.data, 0, UDP_DATA_MAX);
    if (msg) {
        /* copy up to 100 bytes */
        L = (int)strlen(msg); 
        if (L > UDP_DATA_MAX) 
            L = UDP_DATA_MAX;
        memcpy(r.data, msg, L);
    }
    sendto(s, &r, sizeof(r), 0, (struct sockaddr*)to, alen);
}

static int insert_entry(const char *peer, const char *content, 
                        const char *ip, int port) {
    int i;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (table[i].in_use &&
            strcmp(table[i].peer, peer) == 0 &&
            strcmp(table[i].content, content) == 0) {
            return -2; /* duplicate peer+content */
        }
    }
    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (!table[i].in_use) {
            trim_fix(table[i].peer, peer, 10);
            trim_fix(table[i].content, content, 10);
            trim_fix(table[i].ip, ip, 15);
            table[i].port = port;
            table[i].used = 0;
            table[i].in_use = 1;
            return 0;
        }
    }
    return -1; /* table full */
}

static int remove_entry(const char *peer, const char *content) {
    int i;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (table[i].in_use &&
            strcmp(table[i].peer, peer) == 0 &&
            strcmp(table[i].content, content) == 0) {
            table[i].in_use = 0;
            return 0;
        }
    }
    return -1;
}

static int pick_least_used(const char *content) {
    int best = -1;
    int i;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (table[i].in_use && strcmp(table[i].content, content) == 0) {
            if (best < 0 || table[i].used < table[best].used) 
                best = i;
        }
    }
    return best;
}

int main(int argc, char **argv) {
    int port = (argc >= 2) ? atoi(argv[1]) : DEFAULT_PORT;
    int s;
    struct sockaddr_in sin;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { 
        perror("socket"); 
        return 1; 
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);

    if (bind(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Index Server listening on UDP port %d\n", port);

    while (1) {
        struct sockaddr_in from; 
        socklen_t alen = sizeof(from);
        struct pdu req; 
        ssize_t n;
        
        memset(&req, 0, sizeof(req));
        n = recvfrom(s, &req, sizeof(req), 0, (struct sockaddr*)&from, &alen);
        if (n <= 0) continue;

        if (req.type == 'R') {
            /* Data: peer(10) + content(10) + address string "IP:PORT" */
            char peer[11], content[11], addr[81];
            char *colon;
            char *ip;
            int portnum;
            int r;
            
            extract_fixed(peer, req.data, 0, 10);
            extract_fixed(content, req.data, 10, 10);
            /* the rest is addr string */
            memcpy(addr, req.data + 20, 80); 
            addr[80] = '\0';

            /* parse "IP:PORT" */
            colon = strchr(addr, ':');
            if (!colon) { 
                send_pdu(s, &from, alen, 'E', "BAD_ADDR"); 
                continue; 
            }
            *colon = '\0';
            ip = addr;
            portnum = atoi(colon + 1);

            /* Validate field lengths and content */
            if (strlen(peer) == 0 || strlen(content) == 0 || 
                strlen(ip) == 0 || portnum <= 0) {
                send_pdu(s, &from, alen, 'E', "BAD_FIELDS");
                continue;
            }
            
            /* Check if peer or content names exceed 10 bytes */
            if (strlen(peer) > 10 || strlen(content) > 10) {
                send_pdu(s, &from, alen, 'E', "NAME_TOO_LONG");
                continue;
            }
            
            r = insert_entry(peer, content, ip, portnum);
            if (r == 0) 
                send_pdu(s, &from, alen, 'A', "OK");
            else if (r == -2) 
                send_pdu(s, &from, alen, 'E', "NAME_CONFLICT");
            else 
                send_pdu(s, &from, alen, 'E', "TABLE_FULL");
        }
        else if (req.type == 'S') {
            /* Data: peer(10) + content(10) */
            char peer[11], content[11];
            int idx;
            
            extract_fixed(peer, req.data, 0, 10);
            extract_fixed(content, req.data, 10, 10);

            idx = pick_least_used(content);
            if (idx < 0) {
                send_pdu(s, &from, alen, 'E', "NOT_FOUND");
            } else {
                char out[UDP_DATA_MAX];
                snprintf(out, sizeof(out), "%s:%d", table[idx].ip, table[idx].port);
                send_pdu(s, &from, alen, 'S', out);
                table[idx].used++;
            }
        }
        else if (req.type == 'T') {
            /* Data: peer(10) + content(10) */
            char peer[11], content[11];
            int r;
            
            extract_fixed(peer, req.data, 0, 10);
            extract_fixed(content, req.data, 10, 10);
            r = remove_entry(peer, content);
            if (r == 0) 
                send_pdu(s, &from, alen, 'A', "OK");
            else 
                send_pdu(s, &from, alen, 'E', "NOT_REGISTERED");
        }
        else if (req.type == 'O') {
            /* Reply with a single O PDU listing "peer:content,peer:content,..." */
            /* (If too long, truncate; simple and acceptable for demo.) */
            char list[UDP_DATA_MAX]; 
            int first = 1;
            int i;
            
            list[0] = '\0';
            for (i = 0; i < MAX_ENTRIES; ++i) {
                char item[30];
                int need;
                
                if (!table[i].in_use) continue;
                snprintf(item, sizeof(item), "%s:%s", table[i].peer, table[i].content);
                need = (int)strlen(list) + (first ? 0 : 1) + (int)strlen(item);
                if (need >= UDP_DATA_MAX) break;
                if (!first) 
                    strncat(list, ",", sizeof(list) - strlen(list) - 1);
                strncat(list, item, sizeof(list) - strlen(list) - 1);
                first = 0;
            }
            if (list[0] == '\0') 
                strncpy(list, "(none)", sizeof(list));
            send_pdu(s, &from, alen, 'O', list);
        }
        else {
            send_pdu(s, &from, alen, 'E', "BAD_TYPE");
        }
    }
    return 0;
}
