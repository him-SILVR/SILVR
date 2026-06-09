/*
 * silvr_api.h — SILVR HTTP API for block explorer
 * =================================================
 * Runs on port 8634 in a separate thread.
 * Returns JSON responses for the explorer frontend.
 *
 * Endpoints:
 *   GET /status        → node status, height, supply, balance
 *   GET /blocks        → last 20 blocks
 *   GET /block?h=N     → single block by height
 *   GET /tx?id=TXID    → transaction lookup
 *   GET /balance?addr= → address balance
 */

#ifndef SILVR_API_H
#define SILVR_API_H

#include "silvr_p2p.h"
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

#define API_PORT      8634
#define API_BUF_SIZE  65536

#ifndef SILVR_INITIAL_BITS
#define SILVR_INITIAL_BITS 25
#endif

/* =========================================================================
 * EXTERNAL REFERENCES (defined in silvrd_v3.c)
 * ========================================================================= */
extern silvr_block_t   g_chain[];
extern uint8_t         g_block_hashes[][32];
extern uint64_t        g_chain_height;
extern silvr_keypair_t g_miner_kp;

/* Global API socket */
silvr_socket_t g_api_sock = (silvr_socket_t)-1;

/* =========================================================================
 * HELPERS
 * ========================================================================= */
static void bytes_to_hex(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++)
        sprintf(out + i * 2, "%02x", b[i]);
    out[n * 2] = '\0';
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    if (strlen(hex) != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

/* =========================================================================
 * HTTP RESPONSE
 * ========================================================================= */
static void send_response(silvr_socket_t sock,
                           const char *status,
                           const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, (int)strlen(body));
    send(sock, header, (int)strlen(header), 0);
    send(sock, body,   (int)strlen(body),   0);
}

static void send_options(silvr_socket_t sock) {
    const char *r =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Connection: close\r\n\r\n";
    send(sock, r, (int)strlen(r), 0);
}

/* =========================================================================
 * JSON BUILDERS
 * ========================================================================= */
static void build_status_json(char *buf, size_t sz) {
    char best_hex[65];
    bytes_to_hex(g_best_hash, 32, best_hex);
    uint64_t balance = utxo_get_balance(g_miner_kp.pkhash);
    uint64_t height  = g_chain_height > 0 ? g_chain_height - 1 : 0;

    double bph = 0.0;
    if (g_chain_height >= 10) {
        uint64_t a = (g_chain_height - 10) % 1000;
        uint64_t b2 = (g_chain_height - 1)  % 1000;
        uint32_t t1 = g_chain[a].header.timestamp;
        uint32_t t2 = g_chain[b2].header.timestamp;
        if (t2 > t1) bph = 3600.0 / ((double)(t2 - t1) / 9.0);
    }

    snprintf(buf, sz,
        "{"
        "\"height\":%llu,"
        "\"best_hash\":\"%s\","
        "\"total_supply\":%.8f,"
        "\"max_supply\":42000000.0,"
        "\"miner_address\":\"%s\","
        "\"miner_balance\":%.8f,"
        "\"difficulty\":%u,"
        "\"blocks_per_hour\":%.2f,"
        "\"peers\":%u,"
        "\"version\":\"3.0\","
        "\"chain_id\":2026"
        "}",
        (unsigned long long)height,
        best_hex,
        (double)g_utxo_db.total_supply / SILVR_SATOSHIS,
        g_miner_kp.addr_str,
        (double)balance / SILVR_SATOSHIS,
        SILVR_INITIAL_BITS,
        bph,
        g_peer_count
    );
}

static void build_blocks_json(char *buf, size_t sz) {
    size_t pos = 0;
    pos += snprintf(buf + pos, sz - pos, "[");
    uint64_t start = g_chain_height > 20 ? g_chain_height - 20 : 0;
    int first = 1;
    for (uint64_t i = g_chain_height; i > start; i--) {
        uint64_t idx  = i - 1;
        uint64_t slot = idx % 1000;
        silvr_block_t *b = &g_chain[slot];
        char hash_hex[65], prev_hex[65];
        bytes_to_hex(b->block_hash,       32, hash_hex);
        bytes_to_hex(b->header.prev_hash, 32, prev_hex);
        if (!first) pos += snprintf(buf + pos, sz - pos, ",");
        first = 0;
        pos += snprintf(buf + pos, sz - pos,
            "{"
            "\"height\":%llu,"
            "\"hash\":\"%s\","
            "\"prev_hash\":\"%s\","
            "\"timestamp\":%u,"
            "\"nonce\":%u,"
            "\"difficulty\":%u,"
            "\"n_txs\":%u,"
            "\"reward\":50.0"
            "}",
            (unsigned long long)idx,
            hash_hex, prev_hex,
            b->header.timestamp,
            b->header.nonce,
            b->header.bits,
            b->n_txs
        );
    }
    snprintf(buf + pos, sz - pos, "]");
}

static void build_block_json(char *buf, size_t sz, uint64_t height) {
    if (height >= g_chain_height) {
        snprintf(buf, sz, "{\"error\":\"Block not found\"}");
        return;
    }
    uint64_t slot = height % 1000;
    silvr_block_t *b = &g_chain[slot];
    char hash_hex[65], prev_hex[65], merkle_hex[65];
    bytes_to_hex(b->block_hash,         32, hash_hex);
    bytes_to_hex(b->header.prev_hash,   32, prev_hex);
    bytes_to_hex(b->header.merkle_root, 32, merkle_hex);

    size_t pos = 0;
    pos += snprintf(buf + pos, sz - pos,
        "{"
        "\"height\":%llu,"
        "\"hash\":\"%s\","
        "\"prev_hash\":\"%s\","
        "\"merkle_root\":\"%s\","
        "\"timestamp\":%u,"
        "\"nonce\":%u,"
        "\"difficulty\":%u,"
        "\"n_txs\":%u,"
        "\"txs\":[",
        (unsigned long long)height,
        hash_hex, prev_hex, merkle_hex,
        b->header.timestamp,
        b->header.nonce,
        b->header.bits,
        b->n_txs
    );
    for (uint32_t i = 0; i < b->n_txs && i < 16; i++) {
        char txid_hex[65];
        bytes_to_hex(b->txs[i].txid, 32, txid_hex);
        if (i > 0) pos += snprintf(buf + pos, sz - pos, ",");
        pos += snprintf(buf + pos, sz - pos,
            "{\"txid\":\"%s\",\"n_inputs\":%u,\"n_outputs\":%u}",
            txid_hex, b->txs[i].n_inputs, b->txs[i].n_outputs);
    }
    snprintf(buf + pos, sz - pos, "]}");
}

static void build_tx_json(char *buf, size_t sz, const char *txid_hex) {
    uint8_t txid[32];
    if (hex_to_bytes(txid_hex, txid, 32) != 0) {
        snprintf(buf, sz, "{\"error\":\"Invalid TXID\"}");
        return;
    }
    for (uint64_t i = 0; i < g_chain_height; i++) {
        uint64_t slot = i % 1000;
        for (uint32_t j = 0; j < g_chain[slot].n_txs; j++) {
            if (memcmp(g_chain[slot].txs[j].txid, txid, 32) == 0) {
                silvr_tx_t *tx = &g_chain[slot].txs[j];
                char tid[65]; bytes_to_hex(tx->txid, 32, tid);
                size_t pos = 0;
                pos += snprintf(buf + pos, sz - pos,
                    "{\"txid\":\"%s\",\"block_height\":%llu,"
                    "\"n_inputs\":%u,\"n_outputs\":%u,\"outputs\":[",
                    tid, (unsigned long long)i,
                    tx->n_inputs, tx->n_outputs);
                for (uint32_t k = 0; k < tx->n_outputs; k++) {
                    char ph[41];
                    bytes_to_hex(tx->outputs[k].pubkey_hash, 20, ph);
                    if (k > 0) pos += snprintf(buf + pos, sz - pos, ",");
                    pos += snprintf(buf + pos, sz - pos,
                        "{\"amount\":%.8f,\"pubkey_hash\":\"%s\"}",
                        (double)tx->outputs[k].amount / SILVR_SATOSHIS, ph);
                }
                snprintf(buf + pos, sz - pos, "]}");
                return;
            }
        }
    }
    snprintf(buf, sz, "{\"error\":\"Transaction not found\"}");
}

static void build_balance_json(char *buf, size_t sz, const char *addr) {
    uint8_t pkhash[20];
    silvr_error_t e = crypto_addr_to_pkhash(addr, SILVR_VERSION_BYTE, pkhash);
    if (e != SILVR_OK) {
        snprintf(buf, sz, "{\"error\":\"Invalid address\"}");
        return;
    }
    uint64_t bal = utxo_get_balance(pkhash);
    snprintf(buf, sz,
        "{\"address\":\"%s\",\"balance\":%.8f}",
        addr, (double)bal / SILVR_SATOSHIS);
}

/* =========================================================================
 * REQUEST HANDLER
 * ========================================================================= */
void handle_api_request(silvr_socket_t client) {
    char req[2048] = {0};
    recv(client, req, sizeof(req) - 1, 0);

    if (strncmp(req, "OPTIONS", 7) == 0) {
        send_options(client);
        CLOSE_SOCKET(client);
        return;
    }

    char *buf = (char *)malloc(API_BUF_SIZE);
    if (!buf) { CLOSE_SOCKET(client); return; }

    if (strstr(req, "GET /status")) {
        build_status_json(buf, API_BUF_SIZE);
        send_response(client, "200 OK", buf);

    } else if (strstr(req, "GET /blocks")) {
        build_blocks_json(buf, API_BUF_SIZE);
        send_response(client, "200 OK", buf);

    } else if (strstr(req, "GET /block?h=")) {
        char *p = strstr(req, "h=");
        uint64_t h = p ? (uint64_t)atoll(p + 2) : 0;
        build_block_json(buf, API_BUF_SIZE, h);
        send_response(client, "200 OK", buf);

    } else if (strstr(req, "GET /tx?id=")) {
        char *p = strstr(req, "id=");
        char txid[65] = {0};
        if (p) {
            strncpy(txid, p + 3, 64);
            for (int i = 0; i < 64; i++)
                if (txid[i]==' '||txid[i]=='\r'||
                    txid[i]=='\n'||txid[i]=='&')
                    { txid[i]=0; break; }
        }
        build_tx_json(buf, API_BUF_SIZE, txid);
        send_response(client, "200 OK", buf);

    } else if (strstr(req, "GET /balance?addr=")) {
        char *p = strstr(req, "addr=");
        char addr[40] = {0};
        if (p) {
            strncpy(addr, p + 5, 39);
            for (int i = 0; i < 39; i++)
                if (addr[i]==' '||addr[i]=='\r'||
                    addr[i]=='\n'||addr[i]=='&')
                    { addr[i]=0; break; }
        }
        build_balance_json(buf, API_BUF_SIZE, addr);
        send_response(client, "200 OK", buf);

    } else {
        snprintf(buf, API_BUF_SIZE,
            "{\"endpoints\":[\"/status\",\"/blocks\","
            "\"/block?h=N\",\"/tx?id=TXID\","
            "\"/balance?addr=ADDR\"]}");
        send_response(client, "200 OK", buf);
    }

    free(buf);
    CLOSE_SOCKET(client);
}

/* =========================================================================
 * API INIT — blocking socket for thread use
 * ========================================================================= */
int api_init(void) {
    g_api_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_api_sock == INVALID_SILVR_SOCKET) return -1;

    int opt = 1;
    setsockopt(g_api_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(API_PORT);

    if (bind(g_api_sock,
             (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[API] bind failed on port %d\n", API_PORT);
        return -1;
    }
    listen(g_api_sock, 10);
    /* BLOCKING mode — thread will wait for connections */
    printf("[API] HTTP API listening on port %d\n", API_PORT);
    return 0;
}

/* api_tick — kept for compatibility, does nothing in threaded mode */
static void api_tick(void) {}

#endif /* SILVR_API_H */
