#include "../../include/silvr.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_PEERS        64
#define PEER_TIMEOUT     30
#define MSG_VERSION      1
#define MSG_BLOCK        2
#define MSG_GETBLOCKS    3
#define MSG_PING         4
#define MSG_PONG         5
#define SILVR_MAGIC      0xD1C3A0B2

typedef struct {
    int      fd;
    char     ip[64];
    uint16_t port;
    int      active;
    time_t   last_seen;
    uint32_t version;
    char     user_agent[64];
} silvr_peer_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint32_t length;
    uint8_t  checksum[4];
} silvr_msg_header_t;

static silvr_peer_t peers[MAX_PEERS];
static int          peer_count  = 0;
static int          server_fd   = -1;
static pthread_mutex_t peers_mtx =
    PTHREAD_MUTEX_INITIALIZER;

static const char *seed_nodes[] = {
    "silvr.onrender.com",
    NULL
};

int silvr_net_init(uint16_t port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[NET] socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET,
               SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd,
             (struct sockaddr*)&addr,
             sizeof(addr)) < 0) {
        perror("[NET] bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("[NET] listen");
        close(server_fd);
        return -1;
    }

    printf("[NET] Listening on port %d\n", port);
    printf("[NET] Magic: 0x%08X\n", SILVR_MAGIC);
    printf("[NET] Chain ID: %d\n", SILVR_CHAIN_ID);
    return 0;
}

int silvr_net_connect(const char *ip,
                      uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec  = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET,
               SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET,
               SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip,
                  &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd,
                (struct sockaddr*)&addr,
                sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    pthread_mutex_lock(&peers_mtx);
    if (peer_count < MAX_PEERS) {
        silvr_peer_t *p = &peers[peer_count++];
        p->fd        = fd;
        p->port      = port;
        p->active    = 1;
        p->last_seen = time(NULL);
        strncpy(p->ip, ip, sizeof(p->ip)-1);
        strncpy(p->user_agent, "SILVR/1.0",
                sizeof(p->user_agent)-1);
        printf("[NET] Connected to %s:%d\n",
               ip, port);
    }
    pthread_mutex_unlock(&peers_mtx);
    return fd;
}

void silvr_net_broadcast(const void *data,
                          size_t len,
                          uint32_t msg_type) {
    silvr_msg_header_t hdr;
    hdr.magic  = SILVR_MAGIC;
    hdr.type   = msg_type;
    hdr.length = (uint32_t)len;
    memset(hdr.checksum, 0, 4);

    pthread_mutex_lock(&peers_mtx);
    int sent = 0;
    for (int i = 0; i < peer_count; i++) {
        if (!peers[i].active) continue;
        if (send(peers[i].fd, &hdr,
                 sizeof(hdr), 0) < 0) {
            peers[i].active = 0;
            continue;
        }
        if (data && len > 0) {
            if (send(peers[i].fd,
                     data, len, 0) < 0) {
                peers[i].active = 0;
                continue;
            }
        }
        sent++;
    }
    pthread_mutex_unlock(&peers_mtx);

    if (sent > 0)
        printf("[NET] Broadcast type=%u"
               " to %d peers\n",
               msg_type, sent);
}

void silvr_net_ping_peers(void) {
    silvr_msg_header_t ping;
    ping.magic  = SILVR_MAGIC;
    ping.type   = MSG_PING;
    ping.length = 0;
    memset(ping.checksum, 0, 4);

    pthread_mutex_lock(&peers_mtx);
    time_t now = time(NULL);
    for (int i = 0; i < peer_count; i++) {
        if (!peers[i].active) continue;
        if (now - peers[i].last_seen >
            PEER_TIMEOUT) {
            printf("[NET] Peer %s timed out\n",
                   peers[i].ip);
            close(peers[i].fd);
            peers[i].active = 0;
            continue;
        }
        send(peers[i].fd,
             &ping, sizeof(ping), 0);
    }
    pthread_mutex_unlock(&peers_mtx);
}

void *silvr_net_accept_loop(void *arg) {
    (void)arg;
    while (server_fd >= 0) {
        struct sockaddr_in ca;
        socklen_t ca_len = sizeof(ca);
        int cfd = accept(server_fd,
                         (struct sockaddr*)&ca,
                         &ca_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,
                  &ca.sin_addr,
                  ip, sizeof(ip));
        printf("[NET] New peer: %s\n", ip);

        pthread_mutex_lock(&peers_mtx);
        if (peer_count < MAX_PEERS) {
            silvr_peer_t *p =
                &peers[peer_count++];
            p->fd        = cfd;
            p->port      =
                ntohs(ca.sin_port);
            p->active    = 1;
            p->last_seen = time(NULL);
            strncpy(p->ip, ip,
                    sizeof(p->ip)-1);
        } else {
            close(cfd);
        }
        pthread_mutex_unlock(&peers_mtx);
    }
    return NULL;
}

void silvr_net_shutdown(void) {
    pthread_mutex_lock(&peers_mtx);
    for (int i = 0; i < peer_count; i++) {
        if (peers[i].active)
            close(peers[i].fd);
    }
    peer_count = 0;
    pthread_mutex_unlock(&peers_mtx);
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    printf("[NET] Network shutdown\n");
}

int silvr_net_peer_count(void) {
    int count = 0;
    pthread_mutex_lock(&peers_mtx);
    for (int i = 0; i < peer_count; i++)
        if (peers[i].active) count++;
    pthread_mutex_unlock(&peers_mtx);
    return count;
}

void silvr_net_print_peers(void) {
    pthread_mutex_lock(&peers_mtx);
    printf("\n[NET] === Peers ===\n");
    printf("  Connected: %d\n", peer_count);
    for (int i = 0; i < peer_count; i++) {
        if (peers[i].active)
            printf("  [%d] %s:%d\n",
                   i,
                   peers[i].ip,
                   peers[i].port);
    }
    printf("\n");
    pthread_mutex_unlock(&peers_mtx);
}

void silvr_net_connect_seeds(void) {
    printf("[NET] Connecting to seed nodes\n");
    for (int i = 0; seed_nodes[i]; i++) {
        printf("[NET] Trying seed: %s\n",
               seed_nodes[i]);
    }
}
