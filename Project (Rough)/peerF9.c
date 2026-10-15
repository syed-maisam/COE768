/* COE768 P2P Project - Peer (C89 Compatible) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PACKET_SIZE 100
#define MAX_CONTENT 20

struct PDU {
    char type;
    char data[100];
};

typedef struct {
    int  in_use;
    char name[11];
    int  tcp_sock;
    char port_str[6];
} local_content;

typedef struct {
    char content[32];
    char ip[32];
    int  port;
} search_result;

/* globals */
char peer_name[11];
int  udp_sd;
struct sockaddr_in index_addr;
local_content contents[MAX_CONTENT];

/* ---------- Lab3-style file transfer ---------- */

void receive_file(int sd, char *file_name)
{
    FILE *file;
    char packet[PACKET_SIZE + 1];
    int bytes_received;
    int first_packet;

    file = fopen(file_name, "wb");
    if (file == NULL) {
        printf("File creation failed!\n");
        return;
    }

    first_packet = 1;
    while ((bytes_received = read(sd, packet, sizeof(packet))) > 0) {
        if (first_packet) {
            if (packet[0] == 'E') {
                printf("Server Error: %s\n", packet + 1);
                fclose(file);
                remove(file_name);
                return;
            } else if (packet[0] == 'F') {
                fwrite(packet + 1, 1, bytes_received - 1, file);
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
    int name_len;
    FILE *file;
    char packet[PACKET_SIZE];
    int bytes_read;
    int first_packet;

    memset(file_name, 0, sizeof(file_name));
    name_len = read(sd, file_name, sizeof(file_name) - 1);
    if (name_len <= 0) {
        printf("Failed to read file name from client\n");
        return;
    }
    file_name[name_len] = '\0';

    file = fopen(file_name, "rb");
    if (file == NULL) {
        char error_msg[PACKET_SIZE];
        error_msg[0] = 'E';
        snprintf(error_msg + 1, PACKET_SIZE - 1,
                 "Error: File '%s' not found", file_name);
        write(sd, error_msg, strlen(error_msg));
        return;
    }

    first_packet = 1;
    while ((bytes_read = fread(packet, 1, PACKET_SIZE, file)) > 0) {
        if (first_packet) {
            char temp[PACKET_SIZE + 1];
            temp[0] = 'F';
            memcpy(temp + 1, packet, bytes_read);
            write(sd, temp, bytes_read + 1);
            first_packet = 0;
        } else {
            write(sd, packet, bytes_read);
        }
    }

    fclose(file);
}

/* ---------- UDP helpers ---------- */

int send_pdu(struct PDU *pdu)
{
    ssize_t n;
    n = sendto(udp_sd, pdu, sizeof(*pdu), 0,
               (struct sockaddr *)&index_addr, sizeof(index_addr));
    if (n < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

int recv_pdu(struct PDU *pdu)
{
    ssize_t n;
    socklen_t len;
    struct sockaddr_in from;

    len = sizeof(from);
    n = recvfrom(udp_sd, pdu, sizeof(*pdu), 0,
                 (struct sockaddr *)&from, &len);
    if (n < 0) {
        perror("recvfrom");
        return -1;
    }
    return 0;
}

/* ---------- local content helpers ---------- */

int alloc_slot(void)
{
    int i;
    for (i = 0; i < MAX_CONTENT; i++) {
        if (!contents[i].in_use) {
            return i;
        }
    }
    return -1;
}

int find_slot(const char *name)
{
    int i;
    for (i = 0; i < MAX_CONTENT; i++) {
        if (contents[i].in_use &&
            strcmp(contents[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---------- register / deregister / list ---------- */

void register_content(const char *filename)
{
    int slot;
    int s;
    struct sockaddr_in reg_addr;
    socklen_t alen;
    int port;
    struct PDU pdu;

    slot = alloc_slot();
    if (slot == -1) {
        printf("Local table full, cannot register more content.\n");
        return;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        perror("socket (TCP)");
        return;
    }

    memset(&reg_addr, 0, sizeof(reg_addr));
    reg_addr.sin_family      = AF_INET;
    reg_addr.sin_port        = htons(0);
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

    alen = sizeof(reg_addr);
    if (getsockname(s, (struct sockaddr *)&reg_addr, &alen) == -1) {
        perror("getsockname");
        close(s);
        return;
    }

    port = ntohs(reg_addr.sin_port);

    contents[slot].in_use = 1;
    strncpy(contents[slot].name, filename, 10);
    contents[slot].name[10] = '\0';
    contents[slot].tcp_sock = s;
    snprintf(contents[slot].port_str, sizeof(contents[slot].port_str),
             "%d", port);

    printf("Local: registered '%s' on TCP port %d (slot %d)\n",
           contents[slot].name, port, slot);

    pdu.type = 'R';
    snprintf(pdu.data, sizeof(pdu.data),
             "%s %s %s %d", peer_name, contents[slot].name,
             "127.0.0.1", port);

    if (send_pdu(&pdu) == -1) {
        close(s);
        contents[slot].in_use = 0;
        return;
    }

    if (recv_pdu(&pdu) == -1) {
        return;
    }

    if (pdu.type == 'A') {
        printf("INDEX ACK: %s\n", pdu.data);
    } else if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
        close(s);
        contents[slot].in_use = 0;
    } else {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
    }
}

void deregister_content(const char *filename)
{
    int slot;
    struct PDU pdu;

    slot = find_slot(filename);
    if (slot == -1) {
        printf("Content '%s' not registered locally.\n", filename);
        return;
    }

    pdu.type = 'T';
    snprintf(pdu.data, sizeof(pdu.data),
             "%s %s", peer_name, contents[slot].name);

    if (send_pdu(&pdu) == -1) {
        return;
    }
    if (recv_pdu(&pdu) == -1) {
        return;
    }

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

    if (send_pdu(&pdu) == -1) {
        return;
    }
    if (recv_pdu(&pdu) == -1) {
        return;
    }

    if (pdu.type == 'O') {
        printf("Registered content list:\n%s", pdu.data);
    } else if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
    } else {
        printf("Unexpected PDU from index (type %c)\n", pdu.type);
    }
}

/* ---------- search (UDP only) ---------- */

int search_content(const char *filename, search_result *res)
{
    struct PDU pdu;
    char server_peer[32];
    char content[32];
    char ip[32];
    char port_str[16];
    int parsed;

    pdu.type = 'S';
    snprintf(pdu.data, sizeof(pdu.data),
             "%s %s", peer_name, filename);

    if (send_pdu(&pdu) == -1) {
        return 0;
    }
    if (recv_pdu(&pdu) == -1) {
        return 0;
    }

    if (pdu.type == 'E') {
        printf("INDEX ERROR: %s\n", pdu.data);
        return 0;
    }

    parsed = sscanf(pdu.data, "%31s %31s %31s %15s",
                    server_peer, content, ip, port_str);
    if (parsed != 4) {
        printf("Malformed S response from index: %s\n", pdu.data);
        return 0;
    }

    strcpy(res->content, content);
    strcpy(res->ip, ip);
    res->port = atoi(port_str);

    printf("Index says: peer '%s' serves '%s' at %s:%d\n",
           server_peer, content, res->ip, res->port);

    return 1;
}

/* ---------- download (TCP + Lab3) ---------- */

int download_content(search_result *res)
{
    int sd;
    struct sockaddr_in server;
    int ok;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("socket (TCP client)");
        return 0;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port   = htons(res->port);

    ok = inet_aton(res->ip, &server.sin_addr);
    if (!ok) {
        fprintf(stderr, "Bad server IP address: %s\n", res->ip);
        close(sd);
        return 0;
    }

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("connect (TCP to content server)");
        close(sd);
        return 0;
    }

    write(sd, res->content, strlen(res->content));
    receive_file(sd, (char *)res->content);
    close(sd);

    return 1;
}

/* ---------- incoming download connections ---------- */

void handle_incoming_download(int sock)
{
    struct sockaddr_in client;
    socklen_t clen;
    int new_sd;

    clen = sizeof(client);
    new_sd = accept(sock, (struct sockaddr *)&client, &clen);
    if (new_sd < 0) {
        perror("accept");
        return;
    }

    printf("Incoming download connection...\n");
    send_file(new_sd);
    close(new_sd);
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    int index_port;
    char *index_ip;
    int i;

    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <peer_name> <index_server_ip> <index_server_port>\n",
                argv[0]);
        exit(1);
    }

    strncpy(peer_name, argv[1], 10);
    peer_name[10] = '\0';
    index_ip = argv[2];
    index_port = atoi(argv[3]);

    memset(contents, 0, sizeof(contents));

    udp_sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sd == -1) {
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
        int maxfd;
        int retval;

        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        maxfd = 0;

        for (i = 0; i < MAX_CONTENT; i++) {
            if (contents[i].in_use) {
                FD_SET(contents[i].tcp_sock, &rfds);
                if (contents[i].tcp_sock > maxfd) {
                    maxfd = contents[i].tcp_sock;
                }
            }
        }

        retval = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (retval < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(0, &rfds)) {
            char line[256];
            char cmd;
            char arg[128];
            int scanned;

            arg[0] = '\0';
            if (fgets(line, sizeof(line), stdin) == NULL) {
                line[0] = 'Q';
                line[1] = '\0';
            }

            scanned = sscanf(line, " %c %127s", &cmd, arg);
            if (scanned >= 1) {
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
                        search_result info;
                        int ok_search;
                        int ok_download;

                        ok_search = search_content(arg, &info);
                        if (ok_search) {
                            ok_download = download_content(&info);
                            if (ok_download) {
                                printf("Download complete. Registering content...\n");
                                register_content(arg);
                            }
                        }
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
                    printf("Quitting. Deregistering all content...\n");
                    for (i = 0; i < MAX_CONTENT; i++) {
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

        for (i = 0; i < MAX_CONTENT; i++) {
            if (contents[i].in_use &&
                FD_ISSET(contents[i].tcp_sock, &rfds)) {
                handle_incoming_download(contents[i].tcp_sock);
            }
        }
    }

    close(udp_sd);
    return 0;
}
