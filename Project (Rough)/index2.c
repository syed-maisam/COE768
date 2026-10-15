/* index.c
 * COE768 P2P Index Server
 * Listens on UDP port (default 3000). Handles R,S,T,O,Q requests.
 *
 * - Keeps up to MAX_ENTRIES registrations.
 * - For SEARCH, returns the content server with the least download_count.
 * - PDUs: struct PDU { char type; char data[100]; }
 *
 * Compile: gcc index.c -o index
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAX_ENTRIES 100
#define DATA_SZ 100

struct PDU {
    char type;
    char data[DATA_SZ];
};

typedef struct {
    char content[11];  // content name, up to 10 chars + NUL
    char peer[11];     // peer name, up to 10 chars + NUL
    char addr[48];     // dotted-decimal IP
    char port[8];      // port string
    int used;          // 0/1 if active
    int downloads;     // download count (for least-used selection)
} reg_entry;

int main(int argc, char *argv[]) {
    int udp_sock;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cli_len = sizeof(cliaddr);
    int port = 3000;
    if (argc == 2) port = atoi(argv[1]);

    reg_entry table[MAX_ENTRIES];
    for (int i=0;i<MAX_ENTRIES;i++){ table[i].used = 0; table[i].downloads = 0; }

    if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(udp_sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(udp_sock);
        exit(1);
    }

    printf("Index server listening on UDP port %d\n", port);

    while (1) {
        struct PDU recv_pdu, send_pdu;
        ssize_t r = recvfrom(udp_sock, &recv_pdu, sizeof(recv_pdu), 0,
                             (struct sockaddr*)&cliaddr, &cli_len);
        if (r <= 0) continue;

        memset(&send_pdu, 0, sizeof(send_pdu));

        if (recv_pdu.type == 'O') {
            // list registered contents
            send_pdu.type = 'O';
            send_pdu.data[0] = '\0';
            for (int i=0;i<MAX_ENTRIES;i++){
                if (table[i].used) {
                    if (send_pdu.data[0] != '\0') strncat(send_pdu.data, ",", DATA_SZ- strlen(send_pdu.data)-1);
                    strncat(send_pdu.data, table[i].content, DATA_SZ - strlen(send_pdu.data)-1);
                }
            }
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
        else if (recv_pdu.type == 'R') {
            // R: peer;content;ip;port
            char peer[11]={0}, content[11]={0}, ip[48]={0}, portstr[8]={0};
            int parsed = sscanf(recv_pdu.data, "%10[^;];%10[^;];%47[^;];%7s", peer, content, ip, portstr);
            if (parsed < 4) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "Malformed REGISTER", DATA_SZ-1);
                sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
                continue;
            }
            // check conflict: same peer name & same content
            int conflict = 0;
            for (int i=0;i<MAX_ENTRIES;i++){
                if (table[i].used && strcmp(table[i].peer, peer)==0 && strcmp(table[i].content, content)==0) {
                    conflict = 1; break;
                }
            }
            if (conflict) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "Peer name already registered same content", DATA_SZ-1);
                sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
                continue;
            }
            // find slot
            int placed = 0;
            for (int i=0;i<MAX_ENTRIES;i++){
                if (!table[i].used) {
                    strncpy(table[i].peer, peer, 10);
                    strncpy(table[i].content, content, 10);
                    strncpy(table[i].addr, ip, 47);
                    strncpy(table[i].port, portstr, 7);
                    table[i].used = 1;
                    table[i].downloads = 0;
                    placed = 1;
                    break;
                }
            }
            if (placed) {
                send_pdu.type = 'A';
                strncpy(send_pdu.data, "content has been registered", DATA_SZ-1);
            } else {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "Server full, can't register", DATA_SZ-1);
            }
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
        else if (recv_pdu.type == 'S') {
            // Search: peerToFind;content
            char peerToFind[11]={0}, content[11]={0};
            int parsed = sscanf(recv_pdu.data, "%10[^;];%10s", peerToFind, content);
            if (parsed < 2) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "Malformed SEARCH", DATA_SZ-1);
                sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
                continue;
            }
            // Find least-used entry matching content AND peer name == peerToFind
            // Specification: search request contains peerToFind & content; select a server that matches both
            int best = -1;
            for (int i=0;i<MAX_ENTRIES;i++){
                if (table[i].used && strcmp(table[i].content, content)==0 && strcmp(table[i].peer, peerToFind)==0){
                    if (best == -1 || table[i].downloads < table[best].downloads) best = i;
                }
            }
            if (best == -1) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "No content by this name and peer was found", DATA_SZ-1);
            } else {
                send_pdu.type = 'S';
                // content;peer;ip;port
                snprintf(send_pdu.data, DATA_SZ, "%s;%s;%s;%s",
                         table[best].content, table[best].peer, table[best].addr, table[best].port);
                // increment usage for load-balancing
                table[best].downloads++;
            }
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
        else if (recv_pdu.type == 'T') {
            // Deregister: peer;content
            char peer[11]={0}, content[11]={0};
            int parsed = sscanf(recv_pdu.data, "%10[^;];%10s", peer, content);
            if (parsed < 2) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "Malformed DEREG", DATA_SZ-1);
                sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
                continue;
            }
            int found = 0;
            for (int i=0;i<MAX_ENTRIES;i++){
                if (table[i].used && strcmp(table[i].peer, peer)==0 && strcmp(table[i].content, content)==0) {
                    table[i].used = 0;
                    table[i].downloads = 0;
                    table[i].peer[0]='\0'; table[i].content[0]='\0'; table[i].addr[0]='\0'; table[i].port[0]='\0';
                    found = 1; break;
                }
            }
            if (found) { send_pdu.type = 'A'; strncpy(send_pdu.data, "content has been deregistered", DATA_SZ-1); }
            else { send_pdu.type = 'E'; strncpy(send_pdu.data, "This content is not registered with this peer", DATA_SZ-1); }
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
        else if (recv_pdu.type == 'Q') {
            // Quit: data = peername
            char peer[11]={0};
            sscanf(recv_pdu.data, "%10s", peer);
            int still = 0;
            for (int i=0;i<MAX_ENTRIES;i++){
                if (table[i].used && strcmp(table[i].peer, peer)==0) { still = 1; break; }
            }
            if (still) {
                send_pdu.type = 'E';
                strncpy(send_pdu.data, "unable to close, content still registered to this peer", DATA_SZ-1);
            } else {
                send_pdu.type = 'Q';
                send_pdu.data[0] = '\0';
            }
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
        else {
            // Unknown type
            send_pdu.type = 'E';
            strncpy(send_pdu.data, "Unknown PDU type", DATA_SZ-1);
            sendto(udp_sock, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr*)&cliaddr, cli_len);
        }
    }

    close(udp_sock);
    return 0;
}
