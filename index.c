#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

#define NAME_LEN   10
#define MAX_DATA  1024     // increased for list output
#define MAX_PEERS 32
#define MAX_CONTENTS 128

// PDU types
#define PDU_R 'R'
#define PDU_T 'T'
#define PDU_S 'S'
#define PDU_O 'O'
#define PDU_A 'A'
#define PDU_E 'E'

#pragma pack(push,1)
typedef struct {
  char peer[NAME_LEN];
  char content[NAME_LEN];
  uint32_t ip_be;
  uint16_t port_be;
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
#pragma pack(pop)

typedef struct {
    char content[NAME_LEN+1];
    struct {
        char peer[NAME_LEN+1];
        uint32_t ip_be;
        uint16_t port_be;
        unsigned used;
    } peers[MAX_PEERS];
    size_t peer_count;
} content_entry_t;

static content_entry_t content_db[MAX_CONTENTS];
static size_t content_count = 0;

// -------- helpers ----------
static void die(const char *m){ perror(m); exit(1); }

static int same10(const char *a, const char *b){
    char ta[NAME_LEN+1], tb[NAME_LEN+1];
    memcpy(ta, a, NAME_LEN); ta[NAME_LEN]=0;
    memcpy(tb, b, NAME_LEN); tb[NAME_LEN]=0;
    for(int i=NAME_LEN-1;i>=0;i--){ if(ta[i]==' '||ta[i]==0) ta[i]=0; else break; }
    for(int i=NAME_LEN-1;i>=0;i--){ if(tb[i]==' '||tb[i]==0) tb[i]=0; else break; }
    return strcmp(ta,tb)==0;
}

static void copy_trim(char *dst, const char *src){
    memcpy(dst, src, NAME_LEN); dst[NAME_LEN]=0;
    for(int i=NAME_LEN-1;i>=0;i--){ if(dst[i]==' '||dst[i]==0) dst[i]=0; else break; }
}

// find content entry by name
static content_entry_t* find_content(const char *name){
    for(size_t i=0;i<content_count;i++)
        if(same10(content_db[i].content,name))
            return &content_db[i];
    return NULL;
}

// add a new content entry
static content_entry_t* add_content(const char *name){
    if(content_count >= MAX_CONTENTS) return NULL;
    content_entry_t *c = &content_db[content_count++];
    copy_trim(c->content,name);
    c->peer_count = 0;
    return c;
}

// register a peer for content
static int register_peer(const reg_t *r){
    content_entry_t *c = find_content(r->content);
    if(!c) c = add_content(r->content);
    if(!c) return 0;

    // check if peer already registered
    for(size_t i=0;i<c->peer_count;i++)
        if(same10(c->peers[i].peer, r->peer))
            return 1; // already registered

    if(c->peer_count >= MAX_PEERS) return 0;

    copy_trim(c->peers[c->peer_count].peer, r->peer);
    c->peers[c->peer_count].ip_be = r->ip_be;
    c->peers[c->peer_count].port_be = r->port_be;
    c->peers[c->peer_count].used = 0;
    c->peer_count++;
    return 1;
}

// deregister a peer for content
static void deregister_peer(const reg_t *r){
    content_entry_t *c = find_content(r->content);
    if(!c) return;
    for(size_t i=0;i<c->peer_count;i++){
        if(same10(c->peers[i].peer,r->peer)){
            c->peers[i] = c->peers[c->peer_count-1];
            c->peer_count--;
            break;
        }
    }
}

// pick least-used peer for content
static int pick_peer(content_entry_t *c, size_t *idx){
    if(c->peer_count==0) return 0;
    size_t best=0;
    for(size_t i=1;i<c->peer_count;i++)
        if(c->peers[i].used < c->peers[best].used) best=i;
    *idx = best;
    return 1;
}

// -------- main ----------
int main(int argc, char **argv){
    if(argc!=2){ fprintf(stderr,"Usage: %s <udp_port>\n",argv[0]); return 1; }
    int port = atoi(argv[1]);

    int s = socket(AF_INET,SOCK_DGRAM,0); if(s<0) die("socket");
    struct sockaddr_in me = {0};
    me.sin_family = AF_INET;
    me.sin_port = htons(port);
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(s,(struct sockaddr*)&me,sizeof(me))<0) die("bind");

    printf("Index server listening on UDP %d\n", port);

    for(;;){
        unsigned char in[1+MAX_DATA];
        struct sockaddr_in cli; socklen_t clen=sizeof(cli);
        ssize_t n = recvfrom(s,in,sizeof(in),0,(struct sockaddr*)&cli,&clen);
        if(n<=0) continue;

        char t = in[0];

        if(t==PDU_R && n>=1+(ssize_t)sizeof(reg_t)){
            reg_t r; memcpy(&r,in+1,sizeof(r));
            if(register_peer(&r)){
                unsigned char ack=PDU_A; sendto(s,&ack,1,0,(struct sockaddr*)&cli,clen);
            } else {
                const char *msg="Register failed"; unsigned char out[1+MAX_DATA]; out[0]=PDU_E;
                size_t L=strlen(msg); if(L>MAX_DATA)L=MAX_DATA; memcpy(out+1,msg,L);
                sendto(s,out,1+L,0,(struct sockaddr*)&cli,clen);
            }
        }
        else if(t==PDU_T && n>=1+(ssize_t)sizeof(reg_t)){
            reg_t r; memcpy(&r,in+1,sizeof(r));
            deregister_peer(&r);
            unsigned char ack=PDU_A; sendto(s,&ack,1,0,(struct sockaddr*)&cli,clen);
        }
        else if(t==PDU_S && n>=1+(ssize_t)sizeof(search_req_t)){
            search_req_t rq; memcpy(&rq,in+1,sizeof(rq));
            content_entry_t *c = find_content(rq.content);
            if(!c || c->peer_count==0){
                const char *msg="No such content"; unsigned char out[1+MAX_DATA]; out[0]=PDU_E;
                size_t L=strlen(msg); if(L>MAX_DATA)L=MAX_DATA; memcpy(out+1,msg,L);
                sendto(s,out,1+L,0,(struct sockaddr*)&cli,clen);
            } else {
                size_t idx;
                pick_peer(c,&idx);
                c->peers[idx].used++;
                search_resp_t sr={0};
                memcpy(sr.content, c->content, NAME_LEN);
                sr.ip_be = c->peers[idx].ip_be;
                sr.port_be = c->peers[idx].port_be;
                unsigned char out[1+sizeof(sr)]; out[0]=PDU_S; memcpy(out+1,&sr,sizeof(sr));
                sendto(s,out,sizeof(out),0,(struct sockaddr*)&cli,clen);
            }
        }
        else if(t==PDU_O){
            char buf[MAX_DATA]; size_t off=0;
            for(size_t i=0;i<content_count;i++){
                for(size_t j=0;j<content_db[i].peer_count;j++){
                    char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&content_db[i].peers[j].ip_be,ip,sizeof(ip));
                    unsigned p = ntohs(content_db[i].peers[j].port_be);
                    int wrote = snprintf(buf+off,sizeof(buf)-off,"%-10s %-10s %s:%u used=%u\n",
                                         content_db[i].content, content_db[i].peers[j].peer, ip,p,
                                         content_db[i].peers[j].used);
                    if(wrote<=0 || (size_t)wrote>=sizeof(buf)-off) break;
                    off += (size_t)wrote;
                }
            }
            unsigned char out[1+MAX_DATA]; out[0]=PDU_O; memcpy(out+1,buf,off);
            sendto(s,out,1+off,0,(struct sockaddr*)&cli,clen);
        }
        else{
            const char *msg="Unknown/short PDU"; unsigned char out[1+MAX_DATA]; out[0]=PDU_E;
            size_t L=strlen(msg); if(L>MAX_DATA)L=MAX_DATA; memcpy(out+1,msg,L);
            sendto(s,out,1+L,0,(struct sockaddr*)&cli,clen);
        }
    }
}
