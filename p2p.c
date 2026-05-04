cat > /e/GitHub/SILVR/p2p.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#define SOCKET_ERR INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define CLOSE_SOCKET close
#define SOCKET_ERR -1
#endif

#define SILVR_PORT        8633
#define MAX_PEERS         8
#define MSG_HELLO         0x01
#define MSG_GETBLOCKS     0x02
#define MSG_BLOCK         0x03
#define MSG_PING          0x04
#define MSG_PONG          0x05
#define PEER_FILE         "peers.dat"
#define VERSION           2

typedef struct {
    char     ip[64];
    uint16_t port;
    time_t   last_seen;
    int      connected;
} peer_t;

typedef struct {
    uint8_t  magic[4];
    uint8_t  type;
    uint32_t length;
    uint8_t  checksum[4];
} p2p_header_t;

typedef struct {
    uint32_t version;
    uint64_t height;
    uint8_t  best_hash[32];
    char     user_agent[32];
} hello_msg_t;

static peer_t peers[MAX_PEERS];
static int    peer_count = 0;
static uint64_t local_height = 0;

#ifdef _WIN32
static int net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2,2), &wsa) == 0;
}
static void net_cleanup(void) { WSACleanup(); }
#else
static int net_init(void) { return 1; }
static void net_cleanup(void) {}
#endif

void peers_load(void) {
    FILE *f = fopen(PEER_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && peer_count < MAX_PEERS) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            strncpy(peers[peer_count].ip, line, 63);
            peers[peer_count].port = SILVR_PORT;
            peers[peer_count].connected = 0;
            peer_count++;
        }
    }
    fclose(f);
    printf("[P2P] Loaded %d peers from %s\n", peer_count, PEER_FILE);
}

void peers_save(void) {
    FILE *f = fopen(PEER_FILE, "w");
    if (!f) return;
    for (int i = 0; i < peer_count; i++)
        fprintf(f, "%s\n", peers[i].ip);
    fclose(f);
}

void peer_add(const char *ip) {
    for (int i = 0; i < peer_count; i++)
        if (strcmp(peers[i].ip, ip) == 0) return;
    if (peer_count >= MAX_PEERS) return;
    strncpy(peers[peer_count].ip, ip, 63);
    peers[peer_count].port = SILVR_PORT;
    peers[peer_count].connected = 0;
    peer_count++;
    peers_save();
    printf("[P2P] Added peer: %s\n", ip);
}

void build_header(p2p_header_t *h, uint8_t type, uint32_t len) {
    h->magic[0] = 0xD1;
    h->magic[1] = 0xC3;
    h->magic[2] = 0xA0;
    h->magic[3] = 0xB2;
    h->type   = type;
    h->length = len;
    memset(h->checksum, 0, 4);
}

int send_hello(SOCKET sock, uint64_t height) {
    p2p_header_t hdr;
    hello_msg_t  msg;
    build_header(&hdr, MSG_HELLO, sizeof(msg));
    msg.version = VERSION;
    msg.height  = height;
    memset(msg.best_hash, 0, 32);
    strncpy(msg.user_agent, "silvrd/2.2", 31);
    if (send(sock, (char*)&hdr, sizeof(hdr), 0) < 0) return 0;
    if (send(sock, (char*)&msg, sizeof(msg), 0) < 0) return 0;
    return 1;
}

int send_ping(SOCKET sock) {
    p2p_header_t hdr;
    build_header(&hdr, MSG_PING, 0);
    return send(sock, (char*)&hdr, sizeof(hdr), 0) > 0;
}

int connect_peer(peer_t *p, uint64_t height) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == (SOCKET)SOCKET_ERR) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(p->port);
    inet_pton(AF_INET, p->ip, &addr.sin_addr);

    /* 3 second timeout */
#ifdef _WIN32
    int timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
               (char*)&timeout, sizeof(timeout));
#endif

    printf("[P2P] Connecting to %s:%d...\n", p->ip, p->port);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(sock);
        printf("[P2P] Failed to connect to %s\n", p->ip);
        return 0;
    }

    if (!send_hello(sock, height)) {
        CLOSE_SOCKET(sock);
        return 0;
    }

    p2p_header_t resp;
    int received = recv(sock, (char*)&resp, sizeof(resp), 0);
    if (received > 0) {
        if (resp.magic[0] == 0xD1 && resp.magic[1] == 0xC3) {
            p->connected = 1;
            p->last_seen = time(NULL);
            printf("[P2P] Connected to %s | Type: 0x%02x\n",
                   p->ip, resp.type);

            if (resp.type == MSG_HELLO && resp.length > 0) {
                hello_msg_t their_hello;
                recv(sock, (char*)&their_hello, sizeof(their_hello), 0);
                printf("[P2P] Peer height: %llu | Agent: %s\n",
                       (unsigned long long)their_hello.height,
                       their_hello.user_agent);
                if (their_hello.height > height) {
                    printf("[P2P] Peer is ahead by %llu blocks — syncing needed\n",
                           (unsigned long long)(their_hello.height - height));
                } else {
                    printf("[P2P] We are ahead by %llu blocks\n",
                           (unsigned long long)(height - their_hello.height));
                }
            }
        }
    }

    CLOSE_SOCKET(sock);
    return p->connected;
}

SOCKET start_server(uint16_t port) {
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == (SOCKET)SOCKET_ERR) return (SOCKET)SOCKET_ERR;

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(srv);
        return (SOCKET)SOCKET_ERR;
    }

    listen(srv, 5);
    printf("[P2P] Listening on port %d\n", port);
    return srv;
}

void handle_incoming(SOCKET client, const char *peer_ip,
                     uint64_t height) {
    p2p_header_t hdr;
    int n = recv(client, (char*)&hdr, sizeof(hdr), 0);
    if (n <= 0) return;

    if (hdr.magic[0] != 0xD1 || hdr.magic[1] != 0xC3) {
        printf("[P2P] Bad magic from %s — ignored\n", peer_ip);
        return;
    }

    printf("[P2P] Incoming from %s | Type: 0x%02x\n",
           peer_ip, hdr.type);

    switch (hdr.type) {
        case MSG_HELLO: {
            if (hdr.length >= sizeof(hello_msg_t)) {
                hello_msg_t msg;
                recv(client, (char*)&msg, sizeof(msg), 0);
                printf("[P2P] Hello from %s | Height: %llu | %s\n",
                       peer_ip,
                       (unsigned long long)msg.height,
                       msg.user_agent);
                peer_add(peer_ip);
            }
            send_hello(client, height);
            break;
        }
        case MSG_PING:
            send_ping(client);
            break;
        case MSG_GETBLOCKS:
            printf("[P2P] Block sync request from %s\n", peer_ip);
            break;
        default:
            printf("[P2P] Unknown message type 0x%02x\n", hdr.type);
    }
}

void print_peer_status(void) {
    int connected = 0;
    for (int i = 0; i < peer_count; i++)
        if (peers[i].connected) connected++;
    printf("\n[P2P] Peers: %d known | %d connected\n",
           peer_count, connected);
    for (int i = 0; i < peer_count; i++) {
        printf("  [%d] %s:%d  %s\n", i+1,
               peers[i].ip, peers[i].port,
               peers[i].connected ? "CONNECTED" : "offline");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    printf("\n");
    printf("  +------------------------------------------+\n");
    printf("  |   SILVR P2P Network Manager  v2.2        |\n");
    printf("  |   Port: %-6d  Chain ID: 2026           |\n",
           SILVR_PORT);
    printf("  +------------------------------------------+\n\n");

    if (!net_init()) {
        printf("[P2P] Network init failed\n");
        return 1;
    }

    /* Load chain height from blockchain_v2.dat */
    FILE *f = fopen("blockchain_v2.dat", "rb");
    if (f) {
        typedef struct {
            uint8_t  hdr[128];
            uint8_t  hash[32];
            uint64_t miner_reward;
            uint64_t treasury;
            uint64_t total_mined;
            char     miner_address[64];
        } blk_t;
        blk_t blk;
        while (fread(&blk, sizeof(blk), 1, f) == 1)
            local_height++;
        fclose(f);
    }
    printf("[P2P] Local chain height: %llu\n\n",
           (unsigned long long)local_height);

    /* Add peer from command line */
    if (argc > 1) {
        peer_add(argv[1]);
        printf("[P2P] Added peer from args: %s\n\n", argv[1]);
    }

    peers_load();

    if (argc == 1 && peer_count == 0) {
        printf("Usage:\n");
        printf("  p2p.exe <peer_ip>        — connect to a peer\n");
        printf("  p2p.exe server           — run as listener only\n\n");
        printf("Example:\n");
        printf("  p2p.exe 192.168.1.10\n");
        printf("  p2p.exe 203.0.113.5\n\n");
    }

    /* Start server */
    SOCKET server = start_server(SILVR_PORT);

    /* Connect to all known peers */
    printf("[P2P] Connecting to known peers...\n");
    for (int i = 0; i < peer_count; i++) {
        connect_peer(&peers[i], local_height);
    }

    print_peer_status();

    /* Accept incoming connections */
    if (server != (SOCKET)SOCKET_ERR) {
        printf("[P2P] Waiting for incoming connections...\n");
        printf("[P2P] Press Ctrl+C to stop\n\n");

        fd_set readfds;
        struct timeval tv;

        while (1) {
            FD_ZERO(&readfds);
            FD_SET(server, &readfds);
            tv.tv_sec  = 30;
            tv.tv_usec = 0;

            int sel = select((int)server + 1, &readfds,
                             NULL, NULL, &tv);
            if (sel > 0) {
                struct sockaddr_in client_addr;
                socklen_t clen = sizeof(client_addr);
                SOCKET client = accept(server,
                    (struct sockaddr*)&client_addr, &clen);
                if (client != (SOCKET)SOCKET_ERR) {
                    char peer_ip[64];
                    inet_ntop(AF_INET, &client_addr.sin_addr,
                              peer_ip, sizeof(peer_ip));
                    handle_incoming(client, peer_ip, local_height);
                    CLOSE_SOCKET(client);
                }
            } else {
                /* Ping known peers every 30s */
                for (int i = 0; i < peer_count; i++) {
                    if (peers[i].connected) {
                        printf("[P2P] Pinging %s...\n",
                               peers[i].ip);
                    }
                }
            }
        }
        CLOSE_SOCKET(server);
    }

    net_cleanup();
    return 0;
}
EOF
