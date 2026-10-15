#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define NAME_LEN   10
#define MAX_DATA  100
#define CHUNK   65536  // big chunk for fast transfers

// PDUs
#define PDU_R 'R'  // register
#define PDU_T 'T'  // deregister
#define PDU_S 'S'  // search (req/resp)
#define PDU_O 'O'  // list (resp text)
#define PDU_A 'A'  // ack
#define PDU_E 'E'  // error
#define PDU_D 'D'  // download request (TCP)
#define PDU_C 'C'  // content chunk (TCP)

#pragma pack(push,1)
typedef struct {
  char peer[NAME_LEN];
  char content[NAME_LEN];
  uint32_t ip_be;   // IPv4 (network order)
  uint16_t port_be; // TCP listen port (network order)
} reg_t;

typedef struct {
  char requester[NAME_LEN];
  char content[NAME_LEN];
} search_req_t;

typedef struct {
  char content[NAME_LEN];
  uint32_t ip_be;
  uint16_t port_be;
} search_resp_t;

typedef struct {
  char content[NAME_LEN];
} dl_req_t;
#pragma pack(pop)

// ---- globals ---------------------------------------------------------------
static int udp_sd = -1;
static struct sockaddr_in index_addr;

static char PEER[NAME_LEN];                    // padded peer name
static char SELF_IP[INET_ADDRSTRLEN] = "127.0.0.1";

static int listen_sd = -1;                     // TCP listener
static uint16_t listen_port_be = 0;            // network order
static volatile int srv_running = 1;           // server thread flag

// ---- helpers ---------------------------------------------------------------
static void die(const char *m){ perror(m); exit(1); }

static void pad10(char dst[NAME_LEN], const char *src){
  size_t L;
  memset(dst, ' ', NAME_LEN);
  if(!src) return;
  L = strlen(src);
  if(L > NAME_LEN) L = NAME_LEN;
  memcpy(dst, src, L);
}

// C89-safe rstrip_name
static void rstrip_name(char s[NAME_LEN+1]){
  int i;
  s[NAME_LEN] = '\0';
  for(i = NAME_LEN-1; i >= 0; --i){
    if(s[i]==' ' || s[i]=='\0') s[i] = '\0';
    else break;
  }
}

static int open_tcp_listener(uint16_t *port_be_out){
  int s;
  int opt;
  struct sockaddr_in me;
  socklen_t sl;

  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) die("socket tcp");
  opt = 1;
  if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) die("setsockopt");

  memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET;
  me.sin_port   = htons(0);               // OS chooses port
  me.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(s, (struct sockaddr*)&me, sizeof(me)) < 0) die("bind");
  if(listen(s, 64) < 0) die("listen");

  sl = sizeof(me);
  if(getsockname(s, (struct sockaddr*)&me, &sl) < 0) die("getsockname");
  *port_be_out = me.sin_port;
  return s;
}

// ---- background TCP server -------------------------------------------------
static void *serve_loop(void *arg){
  struct sockaddr_in cli;
  socklen_t cl;
  int ns;
  unsigned char hdr[1 + sizeof(dl_req_t)];
  dl_req_t dr;
  char name[NAME_LEN+1];
  FILE *f;
  unsigned char frame[1 + CHUNK];
  ssize_t got, wr;
  (void)arg;

  for(;;){
    cl = sizeof(cli);
    ns = accept(listen_sd, (struct sockaddr*)&cli, &cl);
    if(ns < 0){
      if(!srv_running) break;
      continue;
    }

    got = read(ns, hdr, sizeof(hdr));
    if(got != (ssize_t)sizeof(hdr) || hdr[0] != PDU_D){
      close(ns);
      continue;
    }

    memcpy(&dr, hdr + 1, sizeof(dr));
    memcpy(name, dr.content, NAME_LEN);
    rstrip_name(name);

    f = fopen(name, "rb");
    if(!f){ close(ns); continue; }

    frame[0] = PDU_C;
    for(;;){
      size_t m = fread(frame + 1, 1, CHUNK, f);
      if(m == 0) break;
      wr = write(ns, frame, (ssize_t)(1 + m));
      if(wr != (ssize_t)(1 + m)) break;
      if(m < CHUNK) break;
    }

    fclose(f);
    close(ns);
  }
  return NULL;
}

// ---- index helpers ---------------------------------------------------------
static void send_register(const char *content_name){
  reg_t r;
  unsigned char out[1 + sizeof(r)];
  unsigned char in[1 + MAX_DATA];
  struct sockaddr_in from;
  socklen_t fl;
  ssize_t n;

  memset(&r, 0, sizeof(r));
  pad10(r.peer, PEER);
  pad10(r.content, content_name);
  inet_pton(AF_INET, SELF_IP, &r.ip_be);
  r.port_be = listen_port_be;

  out[0] = PDU_R; memcpy(out + 1, &r, sizeof(r));
  sendto(udp_sd, out, sizeof(out), 0, (struct sockaddr*)&index_addr, sizeof(index_addr));

  fl = sizeof(from);
  n = recvfrom(udp_sd, in, sizeof(in), 0, (struct sockaddr*)&from, &fl);
  if(n > 0 && in[0] == PDU_A) printf("[Index] ACK (registered '%s')\n", content_name);
  else if(n > 1 && in[0] == PDU_E){ in[n]=0; printf("[Index] ERROR: %s\n", (char*)in+1); }
}

static void send_deregister(const char *content_name){
  reg_t r;
  unsigned char out[1 + sizeof(r)];
  unsigned char in;
  memset(&r, 0, sizeof(r));
  pad10(r.peer, PEER);
  pad10(r.content, content_name);
  out[0] = PDU_T; memcpy(out + 1, &r, sizeof(r));
  sendto(udp_sd, out, sizeof(out), 0, (struct sockaddr*)&index_addr, sizeof(index_addr));
  recvfrom(udp_sd, &in, 1, 0, NULL, 0);
}

static int search_content(const char *content_name, char ip_out[], int *port_out){
  search_req_t rq;
  unsigned char out[1 + sizeof(rq)];
  unsigned char in[1 + MAX_DATA];
  struct sockaddr_in from;
  socklen_t fl;
  ssize_t n;
  search_resp_t sr;

  memset(&rq, 0, sizeof(rq));
  pad10(rq.requester, PEER);
  pad10(rq.content, content_name);
  out[0] = PDU_S; memcpy(out + 1, &rq, sizeof(rq));
  sendto(udp_sd, out, sizeof(out), 0, (struct sockaddr*)&index_addr, sizeof(index_addr));

  fl = sizeof(from);
  n = recvfrom(udp_sd, in, sizeof(in), 0, (struct sockaddr*)&from, &fl);
  if(n > 0 && in[0] == PDU_S){
    memcpy(&sr, in + 1, sizeof(sr));
    inet_ntop(AF_INET, &sr.ip_be, ip_out, INET_ADDRSTRLEN);
    *port_out = (int)ntohs(sr.port_be);
    return 1;
  }
  if(n > 1 && in[0] == PDU_E){ in[n]=0; printf("[Index] ERROR: %s\n", (char*)in+1); }
  return 0;
}

// ---- downloader ------------------------------------------------------------
static int download_from(const char *ip, int port, const char *content_name){
  int s;
  struct sockaddr_in svr;
  unsigned char msg[1 + sizeof(dl_req_t)];
  dl_req_t dr;
  char outname[128];
  FILE *out;
  size_t total;
  unsigned char hdr;
  static unsigned char buf[CHUNK];
  ssize_t r;
 
  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0){ perror("socket tcp"); return 0; }

  memset(&svr, 0, sizeof(svr));
  svr.sin_family = AF_INET;
  svr.sin_port = htons((uint16_t)port);
  if(inet_pton(AF_INET, ip, &svr.sin_addr) != 1){ fprintf(stderr,"inet_pton failed for %s\n", ip); close(s); return 0; }
  if(connect(s, (struct sockaddr*)&svr, sizeof(svr)) < 0){ perror("connect"); close(s); return 0; }

  msg[0] = PDU_D;
  memset(&dr, 0, sizeof(dr));
  pad10(dr.content, content_name);
  memcpy(msg + 1, &dr, sizeof(dr));
  if(write(s, msg, sizeof(msg)) != (ssize_t)sizeof(msg)){ perror("write D"); close(s); return 0; }

  sprintf(outname, "downloaded_%s", content_name);
  out = fopen(outname, "wb"); if(!out){ perror("fopen out"); close(s); return 0; }

  total = 0;
  for(;;){
    r = read(s, &hdr, 1);
    if(r == 0) break;
    if(r < 0){ perror("read hdr"); break; }
    if(hdr != PDU_C){ fprintf(stderr, "Unexpected PDU %c\n", hdr); break; }
    r = read(s, buf, sizeof(buf));
    if(r <= 0) break;
    fwrite(buf, 1, (size_t)r, out);
    total += (size_t)r;
    if(r < (ssize_t)sizeof(buf)) break;
  }

  fclose(out);
  close(s);
  printf("Saved %zu bytes to %s\n", total, outname);
  return 1;
}

// ---- main ------------------------------------------------------------------
int main(int argc, char **argv){
    int choice;
    char line[128];
    char name_in[64], ip_in[64];

    if(argc != 3){
        fprintf(stderr, "Usage: %s <index_ip> <index_udp_port>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    udp_sd = socket(AF_INET, SOCK_DGRAM, 0); if(udp_sd < 0) die("udp socket");
    memset(&index_addr, 0, sizeof(index_addr));
    index_addr.sin_family = AF_INET;
    index_addr.sin_port   = htons((uint16_t)atoi(argv[2]));
    if(inet_pton(AF_INET, argv[1], &index_addr.sin_addr) != 1){ 
        fprintf(stderr,"Bad index IP: %s\n", argv[1]); 
        return 1; 
    }

    // Peer name
    printf("Peer name (<=10 chars): "); fflush(stdout);
    if(!fgets(name_in, sizeof(name_in), stdin)) return 0;
    name_in[strcspn(name_in, "\r\n")] = 0;

    // IPv4 prompt (default to 127.0.0.1 if blank)
    printf("Your IPv4 (Hit enter to use 127.0.0.1 as local default): "); fflush(stdout);
    if(!fgets(ip_in, sizeof(ip_in), stdin)) return 0;
    ip_in[strcspn(ip_in, "\r\n")] = 0;
    if(ip_in[0] != '\0'){
        strncpy(SELF_IP, ip_in, sizeof(SELF_IP)-1);
        SELF_IP[INET_ADDRSTRLEN-1] = '\0';
    }
    // else SELF_IP remains default "127.0.0.1"

    pad10(PEER, name_in);

    listen_sd = open_tcp_listener(&listen_port_be);
    printf("Listening for downloads on TCP %s:%u\n", SELF_IP, (unsigned)ntohs(listen_port_be));
    {
        pthread_t th;
        pthread_create(&th, NULL, serve_loop, NULL);
        pthread_detach(th);
    }

    for(;;){
        printf("\n=== Menu ===\n");
        printf("1) Register content\n");
        printf("2) Deregister content\n");
        printf("3) List & Download\n");
        printf("0) Quit\n> ");
        fflush(stdout);

        if(!fgets(line, sizeof(line), stdin)) break;
        choice = atoi(line);

        if(choice == 1){
            char file[128];
            printf("File to share (must exist here): "); fflush(stdout);
            if(!fgets(file, sizeof(file), stdin)) continue;
            file[strcspn(file, "\r\n")] = 0;
            send_register(file);

        } else if(choice == 2){
            char name2[128];
            printf("Content name to deregister: "); fflush(stdout);
            if(!fgets(name2, sizeof(name2), stdin)) continue;
            name2[strcspn(name2, "\r\n")] = 0;
            send_deregister(name2);

        } else if(choice == 3){
            unsigned char t = PDU_O;
            unsigned char resp[4096];   // bigger buffer for multiple entries
            struct sockaddr_in from;
            socklen_t fl;
            ssize_t n;

            // Request the list from index
