/* peer.c
 * COE768 P2P Peer
 * - Uses UDP to communicate with the index server (host:port).
 * - Uses select() to monitor stdin, UDP socket, and multiple TCP listening sockets.
 * - Creates one TCP passive socket per registered content (port chosen by OS).
 * - Sends R-type registrations with: peername;content;ip;port (ip dotted-decimal).
 * - Downloads content by contacting index (S), then opening TCP to the returned server, sending D with content name,
 *   and receiving multiple C PDUs (100-byte chunks).
 *
 * Compile: gcc peer.c -o peer
 *
 * Usage: ./peer [index-host [index-port]]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define DATA_SZ 100

struct PDU {
    char type;
    char data[DATA_SZ];
};

#define MAX_LISTENERS 50
#define MAX_REGISTERED 50

/* Global state for this peer */
char peer_name[11] = {0};

/* For each content registered locally we keep:
 * - content name
 * - listener socket fd
 * - port string
 */
typedef struct {
    int in_use;
    char content[11];
    int listen_fd;
    char ip[48];
    char port[8];
} local_content;

local_content locals[MAX_REGISTERED];

int udp_sock = -1;
struct sockaddr_in index_addr;

/* helper: send PDU to index (UDP connected socket) */
int send_pdu_to_index(struct PDU *pdu) {
    if (udp_sock < 0) return -1;
    ssize_t w = send(udp_sock, pdu, sizeof(*pdu), 0);
    return (w == sizeof(*pdu)) ? 0 : -1;
}

/* helper: receive PDU from index (blocking recv) */
int recv_pdu_from_index(struct PDU *pdu) {
    if (udp_sock < 0) return -1;
    ssize_t r = recv(udp_sock, pdu, sizeof(*pdu), 0);
    return (r == sizeof(*pdu)) ? 0 : -1;
}

/* create a listening TCP socket bound to INADDR_ANY and port 0; returns fd and writes portStr & ipStr */
int create_listener(char *portStr, size_t portLen, char *ipStr, size_t ipLen) {
    int sfd;
    struct sockaddr_in addr;
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    int opt = 1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0); // ask OS for port

    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sfd); return -1;
    }
    if (listen(sfd, 5) < 0) { close(sfd); return -1; }

    socklen_t alen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr*)&addr, &alen) < 0) {
        close(sfd); return -1;
    }
    // port in host order
    unsigned short port = ntohs(addr.sin_port);
    snprintf(portStr, portLen, "%u", (unsigned)port);

    // Determine local IP to advertise:
    // Use getsockname() on the connected UDP socket to get the actual local IP used to reach index.
    struct sockaddr_in localudp;
    socklen_t len = sizeof(localudp);
    if (getsockname(udp_sock, (struct sockaddr*)&localudp, &len) == 0) {
        const char *ip = inet_ntoa(localudp.sin_addr);
        if (ip) strncpy(ipStr, ip, ipLen-1);
        else strncpy(ipStr, "0.0.0.0", ipLen-1);
    } else {
        strncpy(ipStr, "0.0.0.0", ipLen-1);
    }
    ipStr[ipLen-1]='\0';
    return sfd;
}

/* Register local content: create listener socket, then send R pdu to index */
int register_local_content(const char *content) {
    for (int i=0;i<MAX_REGISTERED;i++){
        if (locals[i].in_use && strcmp(locals[i].content, content)==0) {
            printf("Already serving content '%s'\n", content);
            return -1;
        }
    }
    char portStr[8], ipStr[48];
    int lfd = create_listener(portStr, sizeof(portStr), ipStr, sizeof(ipStr));
    if (lfd < 0) {
        perror("create_listener");
        return -1;
    }
    // send R-type PDU: peer;content;ip;port
    struct PDU pdu;
    memset(&pdu,0,sizeof(pdu));
    pdu.type = 'R';
    snprintf(pdu.data, DATA_SZ, "%s;%s;%s;%s", peer_name, content, ipStr, portStr);
    if (send_pdu_to_index(&pdu) < 0) {
        perror("send R");
        close(lfd);
        return -1;
    }
    // wait for response
    struct PDU resp;
    if (recv_pdu_from_index(&resp) < 0) {
        perror("recv A/E");
        close(lfd);
        return -1;
    }
    if (resp.type == 'A') {
        // store local listener in table
        for (int i=0;i<MAX_REGISTERED;i++){
            if (!locals[i].in_use) {
                locals[i].in_use = 1;
                strncpy(locals[i].content, content, 10);
                locals[i].listen_fd = lfd;
                strncpy(locals[i].ip, ipStr, sizeof(locals[i].ip)-1);
                strncpy(locals[i].port, portStr, sizeof(locals[i].port)-1);
                printf("Registered '%s' as server at %s:%s\n", content, ipStr, portStr);
                return 0;
            }
        }
        // table full
        printf("Local registration table full\n");
        close(lfd);
        return -1;
    } else {
        printf("Index returned error on register: %s\n", resp.data);
        close(lfd);
        return -1;
    }
}

/* Deregister one local content */
int deregister_local_content(const char *content) {
    // send T: peer;content
    struct PDU pdu, resp;
    memset(&pdu,0,sizeof(pdu));
    pdu.type = 'T';
    snprintf(pdu.data, DATA_SZ, "%s;%s", peer_name, content);
    if (send_pdu_to_index(&pdu) < 0) { perror("send T"); return -1; }
    if (recv_pdu_from_index(&resp) < 0) { perror("recv T resp"); return -1; }
    if (resp.type == 'A') {
        // remove local listener entry if present
        for (int i=0;i<MAX_REGISTERED;i++){
            if (locals[i].in_use && strcmp(locals[i].content, content)==0) {
                close(locals[i].listen_fd);
                locals[i].in_use = 0;
                locals[i].listen_fd = -1;
                locals[i].content[0] = '\0';
                locals[i].ip[0] = '\0';
                locals[i].port[0] = '\0';
                printf("Deregistered '%s'\n", content);
                return 0;
            }
        }
        // might succeed even if not in locals (we weren't serving it)
        return 0;
    } else {
        printf("Index error on deregister: %s\n", resp.data);
        return -1;
    }
}

/* Download content from a peer at peer_ip:peer_port, content name */
int download_from_peer(const char *peer_ip, const char *peer_port, const char *content) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(atoi(peer_port));
    if (inet_aton(peer_ip, &serv.sin_addr) == 0) {
        printf("Invalid peer IP %s\n", peer_ip);
        close(sock); return -1;
    }
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(sock); return -1;
    }
    // Send D-type: data = peername;content
    struct PDU req;
    memset(&req,0,sizeof(req));
    req.type = 'D';
    snprintf(req.data, DATA_SZ, "%s;%s", peer_name, content);
    if (write(sock, &req, sizeof(req)) != sizeof(req)) { perror("write D"); close(sock); return -1; }

    // Open file for writing (binary)
    FILE *fp = fopen(content, "wb");
    if (!fp) { perror("fopen"); close(sock); return -1; }

    // Read until connection closes. Expect C-type PDUs with data chunks (up to DATA_SZ)
    struct PDU chunk;
    int total_chunks = 0;
    ssize_t r;
    while ((r = read(sock, &chunk, sizeof(chunk))) > 0) {
        if (chunk.type == 'C') {
            // write data exactly DATA_SZ or until zero? The sender may send less meaningful bytes in last chunk.
            // We'll write until a NUL or DATA_SZ bytes. Use strlen up to DATA_SZ to be conservative for text files.
            size_t to_write = strnlen(chunk.data, DATA_SZ);
            if (to_write > 0) fwrite(chunk.data, 1, to_write, fp);
            total_chunks++;
        } else if (chunk.type == 'E') {
            printf("Peer error: %s\n", chunk.data);
            break;
        } else {
            // ignore unknown types
        }
    }
    fclose(fp);
    close(sock);
    if (total_chunks > 0) {
        printf("Downloaded '%s' (%d chunks)\n", content, total_chunks);
        // After download, register this content locally so we become a server for it
        register_local_content(content);
        return 0;
    } else {
        // remove empty file
        remove(content);
        printf("Download failed or empty\n");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    char *idx_host = "localhost";
    int idx_port = 3000;
    if (argc >= 2) idx_host = argv[1];
    if (argc >= 3) idx_port = atoi(argv[2]);

    // init locals
    for (int i=0;i<MAX_REGISTERED;i++){ locals[i].in_use = 0; locals[i].listen_fd = -1; }

    // Create UDP socket and connect to index (so we can send/recv simply)
    if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { perror("udp socket"); exit(1); }
    memset(&index_addr,0,sizeof(index_addr));
    index_addr.sin_family = AF_INET;
    index_addr.sin_port = htons(idx_port);
    struct hostent *hp = gethostbyname(idx_host);
    if (!hp) { fprintf(stderr,"Unknown index host %s\n", idx_host); exit(1); }
    memcpy(&index_addr.sin_addr, hp->h_addr, hp->h_length);

    if (connect(udp_sock, (struct sockaddr*)&index_addr, sizeof(index_addr)) < 0) { perror("connect udp"); exit(1); }

    // Ask for peer name
    printf("Enter Peer Name (<=10 chars): ");
    if (fgets(peer_name, sizeof(peer_name), stdin) == NULL) exit(1);
    peer_name[strcspn(peer_name, "\n")] = 0;
    if (strlen(peer_name) == 0) { printf("Invalid peer name\n"); exit(1); }

    // Main loop using select() on stdin, udp_sock, and all listening TCP sockets
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = udp_sock;
        FD_SET(udp_sock, &rfds);
        FD_SET(0, &rfds); // stdin
        if (0 > maxfd) maxfd = 0;

        // add listeners
        for (int i=0;i<MAX_REGISTERED;i++){
            if (locals[i].in_use && locals[i].listen_fd >= 0) {
                FD_SET(locals[i].listen_fd, &rfds);
                if (locals[i].listen_fd > maxfd) maxfd = locals[i].listen_fd;
            }
        }

        int sel = select(maxfd+1, &rfds, NULL, NULL, NULL);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // stdin ready => read a command
        if (FD_ISSET(0, &rfds)) {
            char line[128];
            if (fgets(line, sizeof(line), stdin) == NULL) { continue; }
            // take first non-space char
            char cmd = 0;
            for (size_t i=0;i<strlen(line);i++) if (line[i] > ' ') { cmd = line[i]; break; }
            if (!cmd) continue;

            if (cmd == 'O') {
                struct PDU pdu, resp;
                memset(&pdu,0,sizeof(pdu));
                pdu.type = 'O';
                strncpy(pdu.data, "list", DATA_SZ-1);
                send_pdu_to_index(&pdu);
                if (recv_pdu_from_index(&resp) == 0 && resp.type == 'O') {
                    printf("Available Content: %s\n", resp.data);
                } else {
                    printf("No response or error from index\n");
                }
            } else if (cmd == 'R') {
                printf("Enter content name to register (<=10 chars): ");
                char content[64];
                if (fgets(content, sizeof(content), stdin) == NULL) continue;
                content[strcspn(content, "\n")] = 0;
                if (strlen(content) == 0 || strlen(content) > 10) { printf("Invalid content name\n"); continue; }
                register_local_content(content);
            } else if (cmd == 'S') {
                printf("Enter content name to search (<=10 chars): ");
                char content[64];
                if (fgets(content, sizeof(content), stdin) == NULL) continue;
                content[strcspn(content, "\n")] = 0;
                if (strlen(content) == 0 || strlen(content) > 10) { printf("Invalid content name\n"); continue; }
                struct PDU pdu, resp;
                memset(&pdu,0,sizeof(pdu));
                pdu.type = 'S';
                // send peer name + content as per spec: peername;content
                snprintf(pdu.data, DATA_SZ, "%s;%s", peer_name, content);
                send_pdu_to_index(&pdu);
                if (recv_pdu_from_index(&resp) == 0) {
                    if (resp.type == 'S') {
                        // parse content;peer;ip;port
                        char c[11], p[11], ip[48], port[8];
                        if (sscanf(resp.data, "%10[^;];%10[^;];%47[^;];%7s", c, p, ip, port) == 4) {
                            printf("Search result: content=%s peer=%s ip=%s port=%s\n", c, p, ip, port);
                        } else {
                            printf("Malformed SEARCH response: %s\n", resp.data);
                        }
                    } else {
                        printf("Index error: %s\n", resp.data);
                    }
                } else {
                    printf("No response from index\n");
                }
            } else if (cmd == 'D') {
                // Download flow: ask user for content & peer name, send S to index specifying peer;content:
                printf("Content to download (<=10 chars): ");
                char content[64];
                if (fgets(content, sizeof(content), stdin) == NULL) continue;
                content[strcspn(content, "\n")] = 0;
                printf("Peer (server) name to download from (<=10 chars): ");
                char serverpeer[64];
                if (fgets(serverpeer, sizeof(serverpeer), stdin) == NULL) continue;
                serverpeer[strcspn(serverpeer, "\n")] = 0;
                struct PDU pdu, resp;
                memset(&pdu,0,sizeof(pdu));
                pdu.type = 'S';
                printf(pdu.data, DATA_SZ, "%s;%s", serverpeer, content);
                send_pdu_to_index(&pdu);
                if (recv_pdu_from_index(&resp) == 0 && resp.type == 'S') {
                    char c[11], p[11], ip[48], port[8];
                    if (sscanf(resp.data, "%10[^;];%10[^;];%47[^;];%7s", c, p, ip, port) == 4) {
                        download_from_peer(ip, port, c);
                    } else {
                        printf("Malformed SEARCH response: %s\n", resp.data);
                    }
                } else {
                    printf("Index error or no response: %s\n", resp.data);
                }
            } else if (cmd == 'T') {
                printf("Enter content name to deregister (<=10 chars): ");
                char content[64];
                if (fgets(content, sizeof(content), stdin) == NULL) continue;
                content[strcspn(content, "\n")] = 0;
                deregister_local_content(content);
            } else if (cmd == 'Q') {
                // Deregister all local content first
                for (int i=0;i<MAX_REGISTERED;i++){
                    if (locals[i].in_use) {
                        // attempt deregister
                        deregister_local_content(locals[i].content);
                    }
                }
                // Now send Q to index
                struct PDU pdu, resp;
                memset(&pdu,0,sizeof(pdu));
                pdu.type = 'Q';
                snprintf(pdu.data, DATA_SZ, "%s", peer_name);
                send_pdu_to_index(&pdu);
                if (recv_pdu_from_index(&resp) == 0 && resp.type == 'Q') {
                    printf("Quit acknowledged by index. Exiting.\n");
                    close(udp_sock);
                    return 0;
                } else {
                    printf("Index refused quit: %s\n", resp.data);
                }
            } else {
                printf("Unknown command '%c'. Valid: R,O,D,S,T,Q\n", cmd);
            }
        }

        // UDP socket has incoming (from index): process if needed (but peer mostly requests and reads immediately)
        if (FD_ISSET(udp_sock, &rfds)) {
            // We'll just read to clear any unsolicited messages
            struct PDU in;
            if (recv(udp_sock, &in, sizeof(in), 0) > 0) {
                // print incoming if not empty
                if (in.type == 'O' || in.type == 'S' || in.type == 'A' || in.type == 'E') {
                    printf("Unsolicited message from index: type=%c data=%s\n", in.type, in.data);
                }
            }
        }

        // Any listening TCP socket pending -> accept and serve
        for (int i=0;i<MAX_REGISTERED;i++){
            if (locals[i].in_use && locals[i].listen_fd >= 0 && FD_ISSET(locals[i].listen_fd, &rfds)) {
                struct sockaddr_in cli;
                socklen_t clen = sizeof(cli);
                int conn = accept(locals[i].listen_fd, (struct sockaddr*)&cli, &clen);
                if (conn < 0) { perror("accept"); continue; }
                // Read incoming PDU (expect D-type)
                struct PDU in;
                ssize_t r = read(conn, &in, sizeof(in));
                if (r == sizeof(in) && in.type == 'D') {
                    // parse content name: may be peername;content or just content depending on client impl
                    char peerpart[11]={0}, contentpart[11]={0};
                    if (sscanf(in.data, "%10[^;];%10s", peerpart, contentpart) >= 1) {
                        // if only 1 parsed, it might be contentpart in peerpart
                        if (contentpart[0] == '\0') strncpy(contentpart, peerpart, 10);
                    }
                    // open local file and stream C-type PDUs (chunks up to DATA_SZ)
                    FILE *fp = fopen(contentpart, "rb");
                    struct PDU out;
                    if (!fp) {
                        out.type = 'E';
                        strncpy(out.data, "File not found", DATA_SZ-1);
                        write(conn, &out, sizeof(out));
                    } else {
                        out.type = 'C';
                        while (1) {
                            memset(out.data,0,DATA_SZ);
                            size_t n = fread(out.data, 1, DATA_SZ, fp);
                            if (n == 0) break;
                            // If last chunk may contain nulls; we send full DATA_SZ but receiver uses strnlen or n.
                            write(conn, &out, sizeof(out));
                            if (n < DATA_SZ) break;
                        }
                        fclose(fp);
                    }
                } else {
                    // ignore or send error
                }
                close(conn);
            }
        }
    }

    close(udp_sock);
    return 0;
}
