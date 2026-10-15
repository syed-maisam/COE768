/* COE768 P2P Project - Peer
 *
 * One program that can:
 *  - Register content with index server (UDP)
 *  - Search & download content via TCP
 *  - Serve content to other peers via TCP
 *  - Maintain one TCP socket per content and use select()
 *
 * Uses Lab3 file download logic for TCP transfer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PACKET_SIZE 100
#define MAX_CONTENT 20
#define SERVER_UDP_PORT 5000

struct PDU {
    char type;
    char data[100];
};

typedef struct {
    int  in_use;
    char name[11]; // content name (file name)
    int  tcp_sock; // passive TCP socket for this content
    char port_str[6];
} local_content;

/* Globals */
char peer_name[11];
int  udp_sd;
struct sockaddr_in index_addr;
local_content contents[MAX_CONTENT];

/* ---------- Lab3-style file transfer functions ---------- */

void receive_file(int sd, char *file_name)
{
    FILE *file = fopen(file_name, "wb");
    if (file == NULL) {
        printf("File creation failed!\n");
        return;
    }

    char packet[PACKET_SIZE + 1];
    int bytes_received;
    int first_packet = 1;

    while ((bytes_received = read(sd, packet, sizeof(packet))) > 0) {
        if (first_packet) {
            if (packet[0] == 'E') {
                printf("Server Error: %s\n", packet + 1);
                fclose(file);
                remove(file_name);
                return;
            } else if (packet[0] == 'F') {
                fwrite(packet + 1, 1, bytes_received - 1, file);
                printf("Receiving file data...\n");
            } else {
                fwrite(packet, 1, bytes_received, file);
            }
            first_packet = 0;
        } else {
            fwrite(packet, 1, bytes_received, file);
        }
    }

    fclose(file);

    if (bytes_received == 0) {
        printf("File '%s' downloaded successfully!\n", file_name);
    } else {
        printf("Error reading from server\n");
    }
}

void send_file(int sd)
{
    char file_name[256];
    memset(file_name, 0, sizeof(file_name));

    int name_len = read(sd, file_name, sizeof(file_name) - 1);
    if (name_len <= 0) {
        printf("Failed to read file name from client\n");
        return;
    }

    file_name[name_len] = '\0';
    printf("FILE REQUEST RECEIVED: %s\n", file_name);

    FILE *file = fopen(file_name, "rb");
    if (file == NULL) {
        printf("-- NO FILE FOUND --\n");
        char error_msg[PACKET_SIZE];
        error_msg[0] = 'E';
        snprintf(error_msg + 1, PACKET_SIZE - 1,
                 "Error: File '%s' not found", file_name);
        write(sd, error_msg, strlen(error_msg));
        return;
    }

    printf("SUCCESSFULLY FOUND FILE: %s\n", file_name);

    char packet[PACKET_SIZE];
    int bytes_read;
    int first_packet = 1;

    while ((bytes_read = fread(packet, 1, PACKET_SIZE, file)) > 0) {
        if (first_packet) {
            char temp[PACKET_SIZE + 1];
            temp[0] = 'F';
            memcpy(temp + 1, packet, bytes_read);
            write(sd, temp, bytes_read + 1);
            printf("Sent first packet: %d bytes (+1 indicator)\n", bytes_read);
            first_packet = 0;
        } else {
            write(sd, packet, bytes_read);
            printf("Sent packet: %d bytes\n", bytes_read);
        }
    }

    fclose(file);
    printf("File transfer complete\n");
}

/* ---------- Helper functions for PDU/UDP ---------- */

int send_pdu(struct PDU *pdu)
{
    ssize_t n = sendto(udp_sd, pdu, sizeof(*pdu), 0,
                       (struct sockaddr *)&index_addr, sizeof(index_addr));
    if (n < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

int recv_pdu(struct PDU *pdu)
{
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(udp_sd, pdu, sizeof(*pdu), 0,
                         (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        perror("recvfrom");
        return -1;
    }
    return 0;
}

/* ---------- Content / Index operations ---------- */

int find_content_slot(const char *name)
{
    for (int i = 0; i < MAX_CONTENT; i++) {
        if (contents[i].in_use &&
            strcmp(contents[i].name, name) == 0)
            return i;
    }
    return -1;
}

int alloc_content_slot(void)
{
    for (int i = 0; i < MAX_CONTENT; i++) {
        if (!contents[i].in_use) return i;
    }
    return -1;
}

void register_content(const char *filename)
{
    int slot = alloc_content_slot();
    if (slot == -1) {
        printf("Local table full, cannot register more content.\n");
        return;
    }

    /* Create TCP passive socket for this content */
    int s;
    struct sockaddr_in reg_addr;
    socklen_t alen = sizeof(reg_addr);

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket (TCP)");
        return;
    }

    memset(&reg_addr, 0, sizeof(reg_addr));
    reg_addr.sin_family      = AF_INET;
    reg_addr.sin_port        = htons(0); // let OS choose
    reg_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&reg_addr, sizeof(reg_addr)) == -1) {
        perror("bind (TCP)");
        close(s);
        return;
    }

    if (listen(s, 5) == -1) {
        perror("listen (TCP)");
        close(s);
        return;
    }

    if (getsockname(s, (struct sockaddr *)&reg_addr, &alen) == -1) {
        perror("getsockname");
        close(s);
        return;
    }

    int port = ntohs(reg_addr.sin_port);

    contents[slot].in_use   = 1;
    strncpy(contents[slot].name, filename, 10);
    contents[slot].name[10] = '\0';
    contents[slot].tcp_sock = s;
    snprintf(contents[slot].port_str, sizeof(contents[slot].port_str), "%d", port);

    printf("Local: registered '%s' on TCP port %d (slot %d)\n",
           contents[slot].name, port, slot);

    /* Send R-type PDU to index server: "peer content ip port" */
    struct PDU pdu;
    pdu.type = 'R';
    // For demo, advertise 127.0.0.1 as IP (all peers on same machine)
    snprintf(pdu.data, sizeof(pdu.data),
             "%s %s %s %d", peer_name, contents[slot].name, "127.0.0.1", port);

    if (send_pdu(&pdu) == -1) {
        close(s);
        contents[slot].in_use = 0;
        return;
    }

    // Wait for A or E
    if (recv_pdu(&pdu) == -1) return;

    if (pdu.type == 'A') {
        printf("INDEX ACK: %s\n", pdu.data);
    } else if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
        // roll back local registration if error
        close(s);
        contents[slot].in_use = 0;
    } else {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
    }
}

void deregister_content(const char *filename)
{
    int slot = find_content_slot(filename);
    if (slot == -1) {
        printf("Content '%s' not registered locally.\n", filename);
        return;
    }

    struct PDU pdu;
    pdu.type = 'T';
    snprintf(pdu.data, sizeof(pdu.data),
             "%s %s", peer_name, contents[slot].name);

    if (send_pdu(&pdu) == -1) return;
    if (recv_pdu(&pdu) == -1) return;

    if (pdu.type == 'A') {
        printf("INDEX ACK: %s\n", pdu.data);
    } else if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
    } else {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
    }

    close(contents[slot].tcp_sock);
    contents[slot].in_use = 0;
}

void list_from_index(void)
{
    struct PDU pdu;
    pdu.type = 'O';
    pdu.data[0] = '\0';

    if (send_pdu(&pdu) == -1) return;
    if (recv_pdu(&pdu) == -1) return;

    if (pdu.type == 'O') {
        printf("Registered content list:\n%s", pdu.data);
    } else if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
    } else {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
    }
}

/* Search in index and download using Lab3 logic;
   then automatically register as content server. */
void search_and_download(const char *filename)
{
    struct PDU pdu;
    pdu.type = 'S';
    snprintf(pdu.data, sizeof(pdu.data), "%s %s", peer_name, filename);

    if (send_pdu(&pdu) == -1) return;
    if (recv_pdu(&pdu) == -1) return;

    if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
        return;
    }

    if (pdu.type != 'S') {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
        return;
    }

    char server_peer[32], content[32], ip[32], port_str[16];
    if (sscanf(pdu.data, "%31s %31s %31s %15s",
               server_peer, content, ip, port_str) != 4) {
        printf("Malformed S response from index: %s\n", pdu.data);
        return;
    }

    int port = atoi(port_str);
    printf("Index says: peer '%s' serves '%s' at %s:%d\n",
           server_peer, content, ip, port);

    /* Create TCP client socket */
    int sd;
    struct sockaddr_in server;

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket (TCP client)");
        return;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port   = htons(port);

    if (!inet_aton(ip, &server.sin_addr)) {
        fprintf(stderr, "Bad server IP address: %s\n", ip);
        close(sd);
        return;
    }

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("connect (TCP to content server)");
        close(sd);
        return;
    }

    /* Lab3 style: send filename, then receive file */
    write(sd, filename, strlen(filename));
    receive_file(sd, (char *)filename);
    close(sd);

    /* After download, register as content server for this file */
    printf("Now registering downloaded content '%s' with index...\n", filename);
    register_content(filename);
}

/* ---------- Incoming download handling ---------- */

void handle_incoming_download(int sock)
{
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    int new_sd = accept(sock, (struct sockaddr *)&client, &clen);
    if (new_sd < 0) {
        perror("accept");
        return;
    }

    printf("Incoming download connection...\n");
    send_file(new_sd);
    close(new_sd);
}

/* ---------- Main loop ---------- */

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <peer_name> <index_server_ip> <index_server_port>\n",
                argv[0]);
        exit(1);
    }

    strncpy(peer_name, argv[1], 10);
    peer_name[10] = '\0';

    char *index_ip   = argv[2];
    int   index_port = atoi(argv[3]);

    /* Create UDP socket for talking to index server */
    if ((udp_sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket (UDP)");
        exit(1);
    }

    memset(&index_addr, 0, sizeof(index_addr));
    index_addr.sin_family = AF_INET;
    index_addr.sin_port   = htons(index_port);

    if (!inet_aton(index_ip, &index_addr.sin_addr)) {
        fprintf(stderr, "Bad index server IP address.\n");
        exit(1);
    }

    memset(contents, 0, sizeof(contents));

    printf("Peer '%s' started. Connected to index server %s:%d\n",
           peer_name, index_ip, index_port);
    printf("Commands:\n");
    printf("  R <filename>  - register content\n");
    printf("  S <filename>  - search & download content\n");
    printf("  O             - list registered content\n");
    printf("  T <filename>  - deregister content\n");
    printf("  Q             - quit\n\n");

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(0, &rfds); // stdin

        int maxfd = 0;

        // Add all TCP passive sockets
        for (int i = 0; i < MAX_CONTENT; i++) {
            if (contents[i].in_use) {
                FD_SET(contents[i].tcp_sock, &rfds);
                if (contents[i].tcp_sock > maxfd)
                    maxfd = contents[i].tcp_sock;
            }
        }

        int retval = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (retval < 0) {
            perror("select");
            break;
        }

        // stdin command
        if (FD_ISSET(0, &rfds)) {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                // EOF on stdin -> behave like Q
                line[0] = 'Q';
                line[1] = '\0';
            }

            char cmd;
            char arg[128];
            arg[0] = '\0';

            if (sscanf(line, " %c %127s", &cmd, arg) >= 1) {
                if (cmd == 'R' || cmd == 'r') {
                    if (arg[0] == '\0') {
                        printf("Usage: R <filename>\n");
                    } else {
                        register_content(arg);
                    }
                } else if (cmd == 'S' || cmd == 's') {
                    if (arg[0] == '\0') {
                        printf("Usage: S <filename>\n");
                    } else {
                        search_and_download(arg);
                    }
                } else if (cmd == 'O' || cmd == 'o') {
                    list_from_index();
                } else if (cmd == 'T' || cmd == 't') {
                    if (arg[0] == '\0') {
                        printf("Usage: T <filename>\n");
                    } else {
                        deregister_content(arg);
                    }
                } else if (cmd == 'Q' || cmd == 'q') {
                    // Deregister all content then quit
                    printf("Quitting. Deregistering all content...\n");
                    for (int i = 0; i < MAX_CONTENT; i++) {
                        if (contents[i].in_use) {
                            deregister_content(contents[i].name);
                        }
                    }
                    close(udp_sd);
                    printf("Goodbye.\n");
                    return 0;
                } else {
                    printf("Unknown command. Use R/S/O/T/Q\n");
                }
            }
        }

        // Incoming TCP connections for downloads
        for (int i = 0; i < MAX_CONTENT; i++) {
            if (contents[i].in_use &&
                FD_ISSET(contents[i].tcp_sock, &rfds)) {
                handle_incoming_download(contents[i].tcp_sock);
            }
        }
    }

    close(udp_sd);
    return 0;
}
