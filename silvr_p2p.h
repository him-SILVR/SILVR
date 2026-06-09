/*
 * silvr_p2p.h — SILVR Protocol P2P Block Sync
 * =============================================
 * Implements full Bitcoin whitepaper Section 5 network flow:
 *   MSG_HELLO      → handshake with nonce challenge
 *   MSG_GETBLOCKS  → request block hashes from a peer
 *   MSG_INV        → announce block/tx inventory
 *   MSG_GETDATA    → request specific block by hash
 *   MSG_BLOCK      → deliver a full block
 *   MSG_PING/PONG  → keepalive
 *   MSG_TX         → broadcast a transaction
 *
 * DIVERGENCE FROM BITCOIN WHITEPAPER:
 *   - Magic bytes 0xD1C3A0B2 instead of 0xD9B4BEF9 (mainnet brand)
 *   - Port 8633 instead of 8333
 *   - Nonce challenge in handshake (Bitcoin doesn't have this)
 *   - Ban score system for rate limiting (Bitcoin has similar in practice)
 *
 * Target: Windows MSYS2 mingw64, gcc 15.2.0, C99
 */

#ifndef SILVR_P2P_H
#define SILVR_P2P_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET silvr_socket_t;
  #define INVALID_SILVR_SOCKET INVALID_SOCKET
  #define CLOSE_SOCKET(s) closesocket(s)
  #define SOCK_ERR WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int silvr_socket_t;
  #define INVALID_SILVR_SOCKET (-1)
  #define CLOSE_SOCKET(s) close(s)
  #define SOCK_ERR errno
#endif

#include "silvr_utxo.h"

/* =========================================================================
 * CONSTANTS
 * ========================================================================= */
#define SILVR_MAGIC          0xD1C3A0B2
#define SILVR_PORT           8633
#define SILVR_MAX_PEERS      64
#define SILVR_MAX_HEADERS    2000  /* max headers per GETBLOCKS response  */
#define SILVR_BAN_THRESHOLD  100   /* ban score limit                      */
#define SILVR_RATE_WINDOW    1     /* seconds per rate window              */
#define SILVR_MAX_MSG_RATE   50    /* max messages per rate window         */
#define SILVR_PROTOCOL_VER   1
#define SILVR_USER_AGENT     "SILVR/2.4"
#define SILVR_HEADER_SIZE    12    /* magic(4) + type(4) + length(4)       */

/* =========================================================================
 * MESSAGE TYPES
 * ========================================================================= */
typedef enum {
    MSG_HELLO     = 0x01,
    MSG_HELLO_ACK = 0x02,
    MSG_GETBLOCKS = 0x03,
    MSG_INV       = 0x04,
    MSG_GETDATA   = 0x05,
    MSG_BLOCK     = 0x06,
    MSG_TX        = 0x07,
    MSG_PING      = 0x08,
    MSG_PONG      = 0x09,
    MSG_REJECT    = 0x0A,
} silvr_msg_type_t;

/* =========================================================================
 * BLOCK HEADER STRUCTURE
 * Per Bitcoin whitepaper Section 3/4:
 *   prev_hash chaining + nonce for PoW
 * ========================================================================= */
typedef struct {
    uint32_t version;
    uint8_t  prev_hash[32];
    uint8_t  merkle_root[32];
    uint32_t timestamp;
    uint32_t bits;             /* difficulty target                        */
    uint32_t nonce;
    uint64_t height;
} silvr_block_header_t;

/* =========================================================================
 * FULL BLOCK
 * ========================================================================= */
#define MAX_TXS_PER_BLOCK 8
typedef struct {
    silvr_block_header_t header;
    uint8_t              block_hash[32];
    uint32_t             n_txs;
    silvr_tx_t           txs[MAX_TXS_PER_BLOCK];
} silvr_block_t;

/* =========================================================================
 * PEER STRUCTURE
 * ========================================================================= */
typedef struct {
    silvr_socket_t sock;
    char           ip[46];          /* IPv4 or IPv6 string                 */
    uint16_t       port;
    uint32_t       version;
    uint64_t       height;          /* peer's reported best chain height   */
    uint8_t        best_hash[32];   /* peer's reported best block hash     */
    uint64_t       nonce;           /* our challenge nonce sent to peer    */
    uint8_t        nonce_verified;  /* 1 = peer echoed correct nonce       */
    uint32_t       msg_count;       /* messages in current rate window     */
    time_t         last_msg_time;   /* start of current rate window        */
    int            ban_score;       /* accumulated misbehavior score       */
    uint8_t        connected;       /* 1 = active connection               */
    time_t         last_ping;       /* last ping sent                      */
} silvr_peer_t;

/* =========================================================================
 * WIRE MESSAGE HEADER
 * All fields big-endian (network byte order) on the wire.
 * ========================================================================= */
typedef struct {
    uint32_t magic;    /* SILVR_MAGIC                                       */
    uint32_t type;     /* silvr_msg_type_t                                  */
    uint32_t length;   /* payload length in bytes                           */
} silvr_msg_header_t;

/* =========================================================================
 * HELLO MESSAGE PAYLOAD
 * ========================================================================= */
typedef struct {
    uint32_t version;
    uint64_t height;
    uint8_t  best_hash[32];
    uint64_t nonce;           /* random challenge — peer must echo this    */
    char     user_agent[32];
} silvr_hello_t;

/* =========================================================================
 * GLOBAL STATE
 * ========================================================================= */
static silvr_peer_t  g_peers[SILVR_MAX_PEERS];
static uint32_t      g_peer_count = 0;
static uint64_t      g_best_height = 0;
static uint8_t       g_best_hash[32];

/* =========================================================================
 * NETWORK INIT / CLEANUP
 * ========================================================================= */
static int p2p_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "[P2P] WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }
#endif
    memset(g_peers, 0, sizeof(g_peers));
    g_peer_count = 0;
    printf("[P2P] Initialized on port %d\n", SILVR_PORT);
    return 0;
}

static void p2p_cleanup(void) {
    for (uint32_t i = 0; i < g_peer_count; i++) {
        if (g_peers[i].connected)
            CLOSE_SOCKET(g_peers[i].sock);
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

/* =========================================================================
 * RATE LIMITING & BAN SCORE
 * ========================================================================= */

/*
 * p2p_check_rate()
 * Returns 1 if peer is within rate limit, 0 if over limit.
 * Adds to ban_score if peer is spamming.
 */
static int p2p_check_rate(silvr_peer_t *peer) {
    time_t now = time(NULL);
    if (now - peer->last_msg_time >= SILVR_RATE_WINDOW) {
        peer->msg_count    = 1;
        peer->last_msg_time = now;
        return 1;
    }
    peer->msg_count++;
    if (peer->msg_count > SILVR_MAX_MSG_RATE) {
        peer->ban_score += 10;
        fprintf(stderr, "[P2P] Rate limit exceeded by %s (ban_score=%d)\n",
                peer->ip, peer->ban_score);
        return 0;
    }
    return 1;
}

/*
 * p2p_is_banned()
 * Returns 1 if peer should be disconnected.
 */
static int p2p_is_banned(const silvr_peer_t *peer) {
    return peer->ban_score >= SILVR_BAN_THRESHOLD;
}

/* =========================================================================
 * MESSAGE SEND HELPERS
 * ========================================================================= */

/*
 * p2p_send_raw()
 * Sends exactly `len` bytes. Returns 0 on success, -1 on error.
 */
static int p2p_send_raw(silvr_socket_t sock,
                         const uint8_t *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, (const char *)(data + sent),
                     (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/*
 * p2p_send_msg()
 * Sends a framed message: header + payload.
 * Header fields written in big-endian (htonl).
 */
static int p2p_send_msg(silvr_socket_t sock,
                         silvr_msg_type_t type,
                         const uint8_t *payload, uint32_t payload_len) {
    uint8_t header[SILVR_HEADER_SIZE];
    uint32_t magic_be  = htonl(SILVR_MAGIC);
    uint32_t type_be   = htonl((uint32_t)type);
    uint32_t length_be = htonl(payload_len);
    memcpy(header,     &magic_be,  4);
    memcpy(header + 4, &type_be,   4);
    memcpy(header + 8, &length_be, 4);

    if (p2p_send_raw(sock, header, SILVR_HEADER_SIZE) != 0) return -1;
    if (payload_len > 0)
        if (p2p_send_raw(sock, payload, payload_len) != 0) return -1;
    return 0;
}

/*
 * p2p_recv_raw()
 * Receives exactly `len` bytes. Returns 0 on success, -1 on error/disconnect.
 */
static int p2p_recv_raw(silvr_socket_t sock, uint8_t *buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        int n = recv(sock, (char *)(buf + recvd),
                     (int)(len - recvd), 0);
        if (n <= 0) return -1;
        recvd += (size_t)n;
    }
    return 0;
}

/*
 * p2p_recv_msg()
 * Reads one framed message from socket.
 * Validates magic bytes and fills type/payload_len.
 * Caller must free *payload_out.
 * Returns 0 on success, -1 on error.
 */
static int p2p_recv_msg(silvr_socket_t sock,
                         silvr_msg_type_t *type_out,
                         uint8_t **payload_out,
                         uint32_t *payload_len_out) {
    uint8_t header[SILVR_HEADER_SIZE];
    if (p2p_recv_raw(sock, header, SILVR_HEADER_SIZE) != 0) return -1;

    uint32_t magic, type_raw, length;
    memcpy(&magic,    header,     4); magic  = ntohl(magic);
    memcpy(&type_raw, header + 4, 4); type_raw = ntohl(type_raw);
    memcpy(&length,   header + 8, 4); length = ntohl(length);

    if (magic != SILVR_MAGIC) {
        fprintf(stderr, "[P2P] Bad magic: 0x%08X\n", magic);
        return -1;
    }
    if (length > 4 * 1024 * 1024) { /* 4MB max message */
        fprintf(stderr, "[P2P] Message too large: %u bytes\n", length);
        return -1;
    }

    *type_out        = (silvr_msg_type_t)type_raw;
    *payload_len_out = length;
    *payload_out     = NULL;

    if (length > 0) {
        *payload_out = (uint8_t *)malloc(length);
        if (!*payload_out) return -1;
        if (p2p_recv_raw(sock, *payload_out, length) != 0) {
            free(*payload_out);
            *payload_out = NULL;
            return -1;
        }
    }
    return 0;
}

/* =========================================================================
 * HANDSHAKE — HELLO WITH NONCE CHALLENGE
 * ========================================================================= */

/*
 * p2p_send_hello()
 * Sends MSG_HELLO with a random nonce challenge.
 * The peer must echo the nonce back in MSG_HELLO_ACK.
 * This prevents replay attacks and trivial spoofing.
 */
static int p2p_send_hello(silvr_peer_t *peer) {
    silvr_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.version = htonl(SILVR_PROTOCOL_VER);
    hello.height  = /* htonll not portable — store as-is for now */
                    g_best_height;
    memcpy(hello.best_hash, g_best_hash, 32);
    strncpy(hello.user_agent, SILVR_USER_AGENT, 31);

    /* Generate random nonce challenge */
    RAND_bytes((uint8_t *)&hello.nonce, 8);
    peer->nonce = hello.nonce;

    return p2p_send_msg(peer->sock, MSG_HELLO,
                         (uint8_t *)&hello, sizeof(hello));
}

/*
 * p2p_handle_hello()
 * Processes incoming MSG_HELLO from a peer.
 * Sends MSG_HELLO_ACK echoing the peer's nonce back.
 */
static int p2p_handle_hello(silvr_peer_t *peer,
                              const uint8_t *payload, uint32_t len) {
    if (len < sizeof(silvr_hello_t)) return -1;
    const silvr_hello_t *hello = (const silvr_hello_t *)payload;

    peer->version = ntohl(hello->version);
    peer->height  = hello->height;
    memcpy(peer->best_hash, hello->best_hash, 32);

    printf("[P2P] Hello from %s — height=%llu agent=%s\n",
           peer->ip,
           (unsigned long long)peer->height,
           hello->user_agent);

    /* Send ACK echoing their nonce */
    uint64_t echo_nonce = hello->nonce;
    return p2p_send_msg(peer->sock, MSG_HELLO_ACK,
                         (uint8_t *)&echo_nonce, sizeof(echo_nonce));
}

/*
 * p2p_handle_hello_ack()
 * Verifies the peer echoed our nonce correctly.
 * Sets peer->nonce_verified = 1 on success.
 */
static int p2p_handle_hello_ack(silvr_peer_t *peer,
                                  const uint8_t *payload, uint32_t len) {
    if (len < 8) return -1;
    uint64_t echoed_nonce;
    memcpy(&echoed_nonce, payload, 8);

    if (echoed_nonce != peer->nonce) {
        fprintf(stderr, "[P2P] Nonce mismatch from %s — possible spoofing\n",
                peer->ip);
        peer->ban_score += 50;
        return -1;
    }

    peer->nonce_verified = 1;
    printf("[P2P] Handshake verified with %s\n", peer->ip);
    return 0;
}

/* =========================================================================
 * BLOCK HASH COMPUTATION
 * ========================================================================= */
static void block_compute_hash(const silvr_block_header_t *hdr,
                                uint8_t *hash_out) {
    crypto_sha256d((const uint8_t *)hdr,
                   sizeof(silvr_block_header_t), hash_out);
}

/* =========================================================================
 * MERKLE ROOT COMPUTATION
 * Per Bitcoin whitepaper Section 7:
 *   "Transactions are hashed in a Merkle Tree with only the root
 *    included in the block's hash."
 * ========================================================================= */

/*
 * merkle_compute_root()
 * Computes Merkle root from array of txids.
 * Standard Bitcoin-style: pair up hashes, SHA256d each pair, repeat.
 * If odd number of hashes, duplicate the last one.
 * Writes 32-byte root to `root_out`.
 */
static void merkle_compute_root(const uint8_t (*txids)[32],
                                  uint32_t n_txs,
                                  uint8_t *root_out) {
    if (n_txs == 0) {
        memset(root_out, 0, 32);
        return;
    }
    if (n_txs == 1) {
        memcpy(root_out, txids[0], 32);
        return;
    }

    /* Working buffer — max 512 hashes per level */
    uint8_t buf[512][32];
    uint32_t count = n_txs;
    for (uint32_t i = 0; i < count; i++)
        memcpy(buf[i], txids[i], 32);

    while (count > 1) {
        uint32_t new_count = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            uint8_t pair[64];
            memcpy(pair,      buf[i], 32);
            /* Duplicate last hash if odd count */
            if (i + 1 < count)
                memcpy(pair + 32, buf[i + 1], 32);
            else
                memcpy(pair + 32, buf[i], 32);
            crypto_sha256d(pair, 64, buf[new_count]);
            new_count++;
        }
        count = new_count;
    }

    memcpy(root_out, buf[0], 32);
}

/* =========================================================================
 * BLOCK VALIDATION
 * Per Bitcoin whitepaper Section 4 & 5:
 *   "Nodes accept the block only if all transactions in it are valid
 *    and not already spent."
 * ========================================================================= */

/*
 * block_validate()
 * Full block validation:
 *   1. PoW: block_hash < target (bits field)
 *   2. prev_hash matches our best known hash
 *   3. Timestamp within ±2 hours
 *   4. Merkle root matches transactions
 *   5. All transactions valid (via tx_validate)
 *
 * Returns 0 if valid, -1 if invalid.
 */
static int block_validate(const silvr_block_t *block,
                           const uint8_t *expected_prev_hash) {
    /* 1. Compute block hash */
    uint8_t hash[32];
    block_compute_hash(&block->header, hash);

    /* Verify hash matches stored hash */
    if (memcmp(hash, block->block_hash, 32) != 0) {
        fprintf(stderr, "[BLOCK] Hash mismatch\n");
        return -1;
    }

    /* Check PoW: first N bits must be zero (simplified check) */
    uint32_t bits = block->header.bits;
    uint32_t zero_bytes = bits / 8;
    for (uint32_t i = 0; i < zero_bytes && i < 32; i++) {
        if (hash[i] != 0) {
            fprintf(stderr, "[BLOCK] PoW check failed at byte %u\n", i);
            return -1;
        }
    }

    /* 2. prev_hash check */
    if (expected_prev_hash &&
        memcmp(block->header.prev_hash, expected_prev_hash, 32) != 0) {
        fprintf(stderr, "[BLOCK] prev_hash mismatch — possible fork\n");
        return -1;
    }

    /* 3. Timestamp check: within ±2 hours of current time */
    uint32_t now = (uint32_t)time(NULL);
    uint32_t ts  = block->header.timestamp;
    if (ts > now + 7200 || ts < now - 7200) {
        fprintf(stderr, "[BLOCK] Timestamp out of range: %u vs now %u\n",
                ts, now);
        return -1;
    }

    /* 4. Merkle root */
    if (block->n_txs > 0) {
        uint8_t txids[MAX_TXS_PER_BLOCK][32];
        for (uint32_t i = 0; i < block->n_txs; i++)
            memcpy(txids[i], block->txs[i].txid, 32);
        uint8_t computed_root[32];
        merkle_compute_root((const uint8_t (*)[32])txids,
                             block->n_txs, computed_root);
        if (memcmp(computed_root, block->header.merkle_root, 32) != 0) {
            fprintf(stderr, "[BLOCK] Merkle root mismatch\n");
            return -1;
        }
    }

    /* 5. Validate all transactions */
    for (uint32_t i = 1; i < block->n_txs; i++) { /* skip coinbase at [0] */
        utxo_error_t e = tx_validate(&block->txs[i]);
        if (e != UTXO_OK) {
            fprintf(stderr, "[BLOCK] TX %u invalid: %s\n",
                    i, utxo_strerror(e));
            return -1;
        }
    }

    return 0;
}

/* =========================================================================
 * BLOCK SYNC MESSAGE HANDLERS
 * Full MSG_GETBLOCKS → MSG_INV → MSG_GETDATA → MSG_BLOCK flow
 * Per Bitcoin whitepaper Section 5
 * ========================================================================= */

/*
 * p2p_send_getblocks()
 * Sends MSG_GETBLOCKS to a peer asking for blocks after `from_hash`.
 * Peer responds with MSG_INV listing available block hashes.
 */
static int p2p_send_getblocks(silvr_peer_t *peer,
                               const uint8_t *from_hash) {
    if (!peer->nonce_verified) {
        fprintf(stderr, "[P2P] Cannot sync with unverified peer %s\n",
                peer->ip);
        return -1;
    }
    printf("[P2P] Sending GETBLOCKS to %s from height %llu\n",
           peer->ip, (unsigned long long)g_best_height);
    return p2p_send_msg(peer->sock, MSG_GETBLOCKS, from_hash, 32);
}

/*
 * p2p_handle_getblocks()
 * Handles incoming MSG_GETBLOCKS from a peer.
 * Responds with MSG_INV containing hashes of blocks we have after from_hash.
 *
 * `block_hashes` and `n_blocks` represent our local chain.
 * In your silvrd_v2.c, pass your actual blockchain array here.
 */
static int p2p_handle_getblocks(silvr_peer_t *peer,
                                  const uint8_t *payload, uint32_t len,
                                  const uint8_t (*block_hashes)[32],
                                  uint64_t n_blocks) {
    if (len < 32) return -1;
    if (!p2p_check_rate(peer)) return -1;

    const uint8_t *from_hash = payload;
    uint8_t zero_hash[32] = {0};

    /* Find from_hash in our chain */
    uint64_t start_idx = 0;
    int found = 0;

    if (memcmp(from_hash, zero_hash, 32) == 0) {
        start_idx = 0; /* start from genesis */
        found = 1;
    } else {
        for (uint64_t i = 0; i < n_blocks; i++) {
            if (memcmp(block_hashes[i], from_hash, 32) == 0) {
                start_idx = i + 1; /* send blocks AFTER this one */
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        fprintf(stderr, "[P2P] GETBLOCKS: from_hash not in our chain\n");
        /* Send from genesis as fallback */
        start_idx = 0;
    }

    /* Build INV payload: count(4) + hashes */
    uint64_t available = (n_blocks > start_idx) ? (n_blocks - start_idx) : 0;
    uint64_t send_count = available < SILVR_MAX_HEADERS
                          ? available : SILVR_MAX_HEADERS;

    if (send_count == 0) {
        printf("[P2P] No new blocks for %s\n", peer->ip);
        return 0;
    }

    size_t inv_size = 4 + send_count * 32;
    uint8_t *inv_buf = (uint8_t *)malloc(inv_size);
    if (!inv_buf) return -1;

    uint32_t count_be = htonl((uint32_t)send_count);
    memcpy(inv_buf, &count_be, 4);
    for (uint64_t i = 0; i < send_count; i++)
        memcpy(inv_buf + 4 + i * 32, block_hashes[start_idx + i], 32);

    printf("[P2P] Sending INV with %llu hashes to %s\n",
           (unsigned long long)send_count, peer->ip);

    int r = p2p_send_msg(peer->sock, MSG_INV, inv_buf, (uint32_t)inv_size);
    free(inv_buf);
    return r;
}

/*
 * p2p_handle_inv()
 * Handles MSG_INV from a peer (list of block hashes they have).
 * For each hash we don't have, sends MSG_GETDATA to request it.
 *
 * `our_hashes` = our local block hash set for lookup.
 */
static int p2p_handle_inv(silvr_peer_t *peer,
                            const uint8_t *payload, uint32_t len,
                            const uint8_t (*our_hashes)[32],
                            uint64_t our_count) {
    if (len < 4) return -1;
    uint32_t count_be;
    memcpy(&count_be, payload, 4);
    uint32_t count = ntohl(count_be);

    if (len < 4 + count * 32) return -1;

    printf("[P2P] INV from %s: %u hashes\n", peer->ip, count);

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *hash = payload + 4 + i * 32;

        /* Check if we already have this block */
        int have = 0;
        for (uint64_t j = 0; j < our_count; j++) {
            if (memcmp(our_hashes[j], hash, 32) == 0) {
                have = 1; break;
            }
        }

        if (!have) {
            /* Send GETDATA for this hash */
            p2p_send_msg(peer->sock, MSG_GETDATA, hash, 32);
            printf("[P2P] Requesting block ");
            for (int b = 0; b < 8; b++) printf("%02X", hash[b]);
            printf("... from %s\n", peer->ip);
        }
    }
    return 0;
}

/*
 * p2p_handle_getdata()
 * Handles MSG_GETDATA: peer wants a specific block.
 * Serializes and sends the block as MSG_BLOCK.
 *
 * `find_block_fn` = callback to look up a block by hash in your chain.
 */
static int p2p_handle_getdata(silvr_peer_t *peer,
                               const uint8_t *payload, uint32_t len,
                               const silvr_block_t *(*find_block_fn)(const uint8_t *hash)) {
    if (len < 32) return -1;
    if (!p2p_check_rate(peer)) return -1;

    const uint8_t *wanted_hash = payload;
    const silvr_block_t *block = find_block_fn(wanted_hash);

    if (!block) {
        fprintf(stderr, "[P2P] GETDATA: block not found\n");
        uint8_t reject[1] = {0x01};
        return p2p_send_msg(peer->sock, MSG_REJECT, reject, 1);
    }

    /* Serialize block: header + n_txs + txs */
    size_t block_size = sizeof(silvr_block_header_t) + 32 /* hash */ +
                        4 /* n_txs */ +
                        block->n_txs * sizeof(silvr_tx_t);
    uint8_t *buf = (uint8_t *)malloc(block_size);
    if (!buf) return -1;

    size_t pos = 0;
    memcpy(buf + pos, &block->header, sizeof(silvr_block_header_t));
    pos += sizeof(silvr_block_header_t);
    memcpy(buf + pos, block->block_hash, 32); pos += 32;
    uint32_t ntxs_be = htonl(block->n_txs);
    memcpy(buf + pos, &ntxs_be, 4); pos += 4;
    for (uint32_t i = 0; i < block->n_txs; i++) {
        memcpy(buf + pos, &block->txs[i], sizeof(silvr_tx_t));
        pos += sizeof(silvr_tx_t);
    }

    printf("[P2P] Sending block h=%llu to %s\n",
           (unsigned long long)block->header.height, peer->ip);

    int r = p2p_send_msg(peer->sock, MSG_BLOCK, buf, (uint32_t)pos);
    free(buf);
    return r;
}

/*
 * p2p_handle_block()
 * Handles incoming MSG_BLOCK from a peer.
 * Validates and applies the block to our chain.
 *
 * Returns 0 if block accepted, -1 if rejected.
 */
static int p2p_handle_block(silvr_peer_t *peer,
                              const uint8_t *payload, uint32_t len,
                              const uint8_t *our_best_hash,
                              uint64_t *our_height_out) {
    size_t min_size = sizeof(silvr_block_header_t) + 32 + 4;
    if (len < min_size) return -1;

    silvr_block_t block;
    memset(&block, 0, sizeof(block));

    size_t pos = 0;
    memcpy(&block.header, payload + pos, sizeof(silvr_block_header_t));
    pos += sizeof(silvr_block_header_t);
    memcpy(block.block_hash, payload + pos, 32); pos += 32;

    uint32_t ntxs_be;
    memcpy(&ntxs_be, payload + pos, 4); pos += 4;
    block.n_txs = ntohl(ntxs_be);

    if (block.n_txs > MAX_TXS_PER_BLOCK) {
        fprintf(stderr, "[P2P] Block has too many txs: %u\n", block.n_txs);
        peer->ban_score += 20;
        return -1;
    }

    for (uint32_t i = 0; i < block.n_txs; i++) {
        if (pos + sizeof(silvr_tx_t) > len) {
            fprintf(stderr, "[P2P] Block payload truncated at tx %u\n", i);
            return -1;
        }
        memcpy(&block.txs[i], payload + pos, sizeof(silvr_tx_t));
        pos += sizeof(silvr_tx_t);
    }

    /* Validate */
    if (block_validate(&block, our_best_hash) != 0) {
        fprintf(stderr, "[P2P] Block validation failed from %s\n", peer->ip);
        peer->ban_score += 10;
        return -1;
    }

    /* Apply all transactions to UTXO set */
    for (uint32_t i = 0; i < block.n_txs; i++) {
        if (i == 0) {
            /* Coinbase — just apply, no validation needed */
            tx_apply(&block.txs[i], block.header.height);
        } else {
            utxo_error_t e = tx_validate(&block.txs[i]);
            if (e != UTXO_OK) {
                fprintf(stderr, "[P2P] TX %u in block invalid: %s\n",
                        i, utxo_strerror(e));
                return -1;
            }
            tx_apply(&block.txs[i], block.header.height);
        }
    }

    /* Update our best height */
    if (our_height_out)
        *our_height_out = block.header.height;

    printf("[P2P] Accepted block h=%llu from %s\n",
           (unsigned long long)block.header.height, peer->ip);

    /* Save UTXO state after each block */
    utxo_save();

    return 0;
}

/* =========================================================================
 * PING / PONG
 * ========================================================================= */
static int p2p_send_ping(silvr_peer_t *peer) {
    uint64_t nonce;
    RAND_bytes((uint8_t *)&nonce, 8);
    peer->last_ping = time(NULL);
    return p2p_send_msg(peer->sock, MSG_PING,
                         (uint8_t *)&nonce, sizeof(nonce));
}

static int p2p_handle_ping(silvr_peer_t *peer,
                             const uint8_t *payload, uint32_t len) {
    if (len < 8) return -1;
    /* Echo the nonce back as PONG */
    return p2p_send_msg(peer->sock, MSG_PONG, payload, 8);
}

/* =========================================================================
 * INCOMING MESSAGE DISPATCHER
 * ========================================================================= */

/*
 * p2p_dispatch()
 * Routes an incoming message to the correct handler.
 * Call this in your main recv loop for each peer.
 *
 * Callback `find_block_fn`: looks up a block by hash in your chain.
 * Pass NULL for nodes that don't serve blocks yet.
 */
static int p2p_dispatch(silvr_peer_t *peer,
                          silvr_msg_type_t type,
                          const uint8_t *payload, uint32_t len,
                          const silvr_block_t *(*find_block_fn)(const uint8_t *),
                          const uint8_t (*our_block_hashes)[32],
                          uint64_t our_block_count,
                          const uint8_t *our_best_hash,
                          uint64_t *our_height_out) {
    /* Reject banned peers immediately */
    if (p2p_is_banned(peer)) {
        fprintf(stderr, "[P2P] Dropping message from banned peer %s\n",
                peer->ip);
        CLOSE_SOCKET(peer->sock);
        peer->connected = 0;
        return -1;
    }

    /* Rate limiting for all message types */
    if (!p2p_check_rate(peer)) return 0;

    switch (type) {
    case MSG_HELLO:
        return p2p_handle_hello(peer, payload, len);

    case MSG_HELLO_ACK:
        return p2p_handle_hello_ack(peer, payload, len);

    case MSG_GETBLOCKS:
        return p2p_handle_getblocks(peer, payload, len,
                                     our_block_hashes, our_block_count);

    case MSG_INV:
        return p2p_handle_inv(peer, payload, len,
                               our_block_hashes, our_block_count);

    case MSG_GETDATA:
        if (find_block_fn)
            return p2p_handle_getdata(peer, payload, len, find_block_fn);
        return 0;

    case MSG_BLOCK:
        return p2p_handle_block(peer, payload, len,
                                 our_best_hash, our_height_out);

    case MSG_PING:
        return p2p_handle_ping(peer, payload, len);

    case MSG_PONG:
        /* Nothing to do for now */
        return 0;

    case MSG_TX:
        /* Phase 4: mempool handling */
        printf("[P2P] MSG_TX received (mempool not yet implemented)\n");
        return 0;

    case MSG_REJECT:
        fprintf(stderr, "[P2P] REJECT received from %s\n", peer->ip);
        return 0;

    default:
        fprintf(stderr, "[P2P] Unknown message type 0x%02X from %s\n",
                type, peer->ip);
        peer->ban_score += 5;
        return 0;
    }
}

/* =========================================================================
 * CONNECT TO PEER
 * ========================================================================= */

/*
 * p2p_connect()
 * Connects to a peer at ip:port and performs handshake.
 * Returns pointer to peer slot or NULL on failure.
 */
static silvr_peer_t *p2p_connect(const char *ip, uint16_t port) {
    if (g_peer_count >= SILVR_MAX_PEERS) {
        fprintf(stderr, "[P2P] Max peers reached\n");
        return NULL;
    }

    silvr_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SILVR_SOCKET) {
        fprintf(stderr, "[P2P] socket() failed\n");
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "[P2P] Invalid IP: %s\n", ip);
        CLOSE_SOCKET(sock);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[P2P] connect() to %s:%u failed: %d\n",
                ip, port, SOCK_ERR);
        CLOSE_SOCKET(sock);
        return NULL;
    }

    silvr_peer_t *peer = &g_peers[g_peer_count++];
    memset(peer, 0, sizeof(*peer));
    peer->sock      = sock;
    peer->port      = port;
    peer->connected = 1;
    peer->last_msg_time = time(NULL);
    strncpy(peer->ip, ip, sizeof(peer->ip) - 1);

    printf("[P2P] Connected to %s:%u\n", ip, port);

    /* Send hello immediately */
    if (p2p_send_hello(peer) != 0) {
        CLOSE_SOCKET(sock);
        peer->connected = 0;
        g_peer_count--;
        return NULL;
    }

    return peer;
}

#endif /* SILVR_P2P_H */
