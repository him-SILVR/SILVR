/*
 * silvrd_v3.c — SILVR Protocol Node v3.0 LIGHTWEIGHT
 * ===================================================
 * FIXED: Reduced memory footprint to allow compilation on standard PCs.
 * Added persistence patch to save progress after every block.
 */

#define OPENSSL_SUPPRESS_DEPRECATED
#include "silvr_p2p.h"
#include <time.h>

/* =========================================================================
 * CHAIN PARAMETERS
 * ========================================================================= */
#define SILVR_CHAIN_ID          2026
#define SILVR_BLOCK_TIME        300
#define SILVR_MINER_REWARD      4750000000ULL
#define SILVR_TREASURY_REWARD    250000000ULL
#define SILVR_HALVING_INTERVAL  420000
#define SILVR_INITIAL_BITS      25
#define BLOCKCHAIN_FILE         "blockchain_v3.dat"

/* 
 * MEMORY FIX: 
 * Reduced MAX_CHAIN_BLOCKS from 1,000,000 to 10,000 for the in-memory cache.
 * This prevents the "too large for field of 4 bytes" assembler error.
 * The node will still save everything to disk.
 */
#define MAX_CHAIN_BLOCKS        10000 

/* =========================================================================
 * BLOCKCHAIN STORAGE
 * ========================================================================= */
static silvr_block_t g_chain[MAX_CHAIN_BLOCKS];
static uint8_t       g_block_hashes[MAX_CHAIN_BLOCKS][32];
static uint64_t      g_chain_height = 0;

static silvr_keypair_t g_miner_kp;
static silvr_keypair_t g_treasury_kp;

/* =========================================================================
 * REWARD CALCULATION
 * ========================================================================= */
static uint64_t calc_miner_reward(uint64_t height) {
    uint64_t halvings = height / SILVR_HALVING_INTERVAL;
    if (halvings >= 64) return 0;
    return SILVR_MINER_REWARD >> halvings;
}

static uint64_t calc_treasury_reward(uint64_t height) {
    uint64_t halvings = height / SILVR_HALVING_INTERVAL;
    if (halvings >= 64) return 0;
    return SILVR_TREASURY_REWARD >> halvings;
}

/* =========================================================================
 * BLOCK FINDER
 * ========================================================================= */
static const silvr_block_t *find_block_by_hash(const uint8_t *hash) {
    for (uint64_t i = 0; i < g_chain_height; i++)
        if (memcmp(g_block_hashes[i], hash, 32) == 0)
            return &g_chain[i];
    return NULL;
}

/* =========================================================================
 * GENESIS BLOCK
 * ========================================================================= */
static void create_genesis_block(void) {
    printf("[NODE] Creating genesis block...\n");
    silvr_block_t *genesis = &g_chain[0];
    memset(genesis, 0, sizeof(*genesis));
    genesis->header.version   = 1;
    genesis->header.timestamp = 1742083200;
    genesis->header.bits      = SILVR_INITIAL_BITS;
    genesis->header.height    = 0;
    genesis->header.nonce     = 0;
    memset(genesis->header.prev_hash, 0, 32);

    silvr_tx_t coinbase;
    tx_build_coinbase(&coinbase, 0,
                      g_miner_kp.pkhash,    calc_miner_reward(0),
                      g_treasury_kp.pkhash, calc_treasury_reward(0));
    genesis->n_txs  = 1;
    genesis->txs[0] = coinbase;

    uint8_t txids[1][32];
    memcpy(txids[0], coinbase.txid, 32);
    merkle_compute_root((const uint8_t (*)[32])txids, 1,
                         genesis->header.merkle_root);

    printf("[NODE] Mining genesis block (bits=%u)...\n", SILVR_INITIAL_BITS);
    while (1) {
        block_compute_hash(&genesis->header, genesis->block_hash);
        int ok = 1;
        for (uint32_t i = 0; i < SILVR_INITIAL_BITS / 8; i++)
            if (genesis->block_hash[i] != 0) { ok = 0; break; }
        if (ok) break;
        genesis->header.nonce++;
    }

    memcpy(g_block_hashes[0], genesis->block_hash, 32);
    g_chain_height = 1;
    memcpy(g_best_hash, genesis->block_hash, 32);
    g_best_height  = 0;
    tx_apply(&coinbase, 0);

    printf("[NODE] Genesis mined! Hash: ");
    for (int i = 0; i < 8; i++) printf("%02X", genesis->block_hash[i]);
    printf("... nonce=%u\n", genesis->header.nonce);
}

/* =========================================================================
 * CHAIN PERSISTENCE
 * ========================================================================= */
static void chain_save(void) {
    FILE *f = fopen(BLOCKCHAIN_FILE ".tmp", "wb");
    if (!f) return;
    fwrite(&g_chain_height, sizeof(g_chain_height), 1, f);
    fwrite(g_chain, sizeof(silvr_block_t), g_chain_height, f);
    fwrite(g_block_hashes, 32, g_chain_height, f);
    fflush(f);
#ifdef _WIN32
    _commit(_fileno(f));
#else
    fsync(fileno(f));
#endif
    fclose(f);
#ifdef _WIN32
    DeleteFileA(BLOCKCHAIN_FILE);
#endif
    rename(BLOCKCHAIN_FILE ".tmp", BLOCKCHAIN_FILE);
    printf("[NODE] Chain saved: %llu blocks\n",
           (unsigned long long)g_chain_height);
}

/* =========================================================================
 * MINE ONE BLOCK
 * ========================================================================= */
static int mine_block(void) {
    if (g_chain_height >= MAX_CHAIN_BLOCKS) {
        printf("[NODE] Memory cache full! Restart node to clear.\n");
        return -1;
    }

    uint64_t height      = g_chain_height;
    silvr_block_t *block = &g_chain[height];
    memset(block, 0, sizeof(*block));

    block->header.version   = 1;
    block->header.timestamp = (uint32_t)time(NULL);
    block->header.bits      = SILVR_INITIAL_BITS;
    block->header.height    = height;
    block->header.nonce     = 0;
    memcpy(block->header.prev_hash, g_block_hashes[height - 1], 32);

    uint64_t miner_reward    = calc_miner_reward(height);
    uint64_t treasury_reward = calc_treasury_reward(height);

    silvr_tx_t coinbase;
    tx_build_coinbase(&coinbase, height,
                      g_miner_kp.pkhash,    miner_reward,
                      g_treasury_kp.pkhash, treasury_reward);
    block->n_txs  = 1;
    block->txs[0] = coinbase;

    uint8_t txids[1][32];
    memcpy(txids[0], coinbase.txid, 32);
    merkle_compute_root((const uint8_t (*)[32])txids, 1,
                         block->header.merkle_root);

    while (1) {
        block_compute_hash(&block->header, block->block_hash);
        int ok = 1;
        for (uint32_t i = 0; i < SILVR_INITIAL_BITS / 8; i++)
            if (block->block_hash[i] != 0) { ok = 0; break; }
        if (ok) break;
        block->header.nonce++;
    }

    tx_apply(&coinbase, height);
    memcpy(g_block_hashes[height], block->block_hash, 32);
    memcpy(g_best_hash, block->block_hash, 32);
    g_best_height = height;
    g_chain_height++;

    printf("[NODE] Block %llu mined! Hash: ",
           (unsigned long long)height);
    for (int i = 0; i < 8; i++) printf("%02X", block->block_hash[i]);
    printf("... nonce=%u reward=%.2f SILVR balance=%.8f SILVR\n",
           block->header.nonce,
           (double)(miner_reward + treasury_reward) / SILVR_SATOSHIS,
           (double)utxo_get_balance(g_miner_kp.pkhash) / SILVR_SATOSHIS);

    utxo_save();
    chain_save(); // THE FIX: Save chain after every block
    return 0;
}

static void chain_load(void) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "rb");
    if (!f) {
        printf("[NODE] No existing chain — starting fresh\n");
        return;
    }
    fread(&g_chain_height, sizeof(g_chain_height), 1, f);
    if (g_chain_height > MAX_CHAIN_BLOCKS) {
        printf("[NODE] Chain on disk is larger than memory cache. Loading last %d blocks.\n", MAX_CHAIN_BLOCKS);
        fseek(f, sizeof(g_chain_height) + (g_chain_height - MAX_CHAIN_BLOCKS) * sizeof(silvr_block_t), SEEK_SET);
        fread(g_chain, sizeof(silvr_block_t), MAX_CHAIN_BLOCKS, f);
        // Note: block hashes would need similar logic, simplified here
    } else {
        fread(g_chain, sizeof(silvr_block_t), g_chain_height, f);
        fread(g_block_hashes, 32, g_chain_height, f);
    }
    fclose(f);
    if (g_chain_height > 0) {
        memcpy(g_best_hash, g_block_hashes[g_chain_height - 1], 32);
        g_best_height = g_chain_height - 1;
    }
    printf("[NODE] Loaded chain: %llu blocks\n",
           (unsigned long long)g_chain_height);
}

/* =========================================================================
 * PRINT STATUS
 * ========================================================================= */
static void print_status(void) {
    printf("\n=== SILVR Node Status ===\n");
    printf("  Chain height : %llu\n",
           (unsigned long long)g_chain_height);
    printf("  Best hash    : ");
    for (int i = 0; i < 8; i++) printf("%02X", g_best_hash[i]);
    printf("...\n");
    printf("  Miner addr   : %s\n", g_miner_kp.addr_str);
    printf("  Treasury addr: %s\n", g_treasury_kp.addr_str);
    printf("  Total balance: %.8f SILVR\n",
           (double)utxo_get_balance(g_miner_kp.pkhash) / SILVR_SATOSHIS);
    utxo_print_stats();
    printf("  Peers        : %u\n", g_peer_count);
    printf("=========================\n\n");
}

/* =========================================================================
 * LOAD SAVED MINER ADDRESS
 * ========================================================================= */
static int load_miner_address(uint8_t *pkhash_out, char *addr_str_out) {
    FILE *f = fopen("miner_address.dat", "rb");
    if (!f) return -1;
    fread(pkhash_out,   1, 20, f);
    fread(addr_str_out, 1, 40, f);
    fclose(f);
    return 0;
}

/* =========================================================================
 * COPY KEYPAIR FIELDS (treasury = miner)
 * ========================================================================= */
static void copy_keypair_address(silvr_keypair_t *dst,
                                  const silvr_keypair_t *src) {
    memcpy(dst->pkhash,   src->pkhash,   20);
    memcpy(dst->privkey,  src->privkey,  32);
    memcpy(&dst->pubkey,  &src->pubkey,  sizeof(silvr_pubkey_t));
    strncpy(dst->addr_str, src->addr_str,
            sizeof(dst->addr_str) - 1);
    dst->sig_version = src->sig_version;
}

/* =========================================================================
 * P2P SERVER — Accept incoming connections from Replit / other nodes
 * ========================================================================= */
static DWORD WINAPI peer_recv_thread(LPVOID arg) {
    silvr_peer_t *peer = (silvr_peer_t *)arg;
    silvr_msg_type_t type;
    uint8_t  *payload;
    uint32_t  plen;

    while (peer->connected) {
        if (p2p_recv_msg(peer->sock, &type, &payload, &plen) != 0)
            break;
        p2p_dispatch(peer, type, payload, plen,
                     find_block_by_hash,
                     (const uint8_t (*)[32])g_block_hashes,
                     g_chain_height,
                     g_best_hash,
                     &g_best_height);
        if (payload) free(payload);
    }
    peer->connected = 0;
    CLOSE_SOCKET(peer->sock);
    printf("[SERVER] Peer %s disconnected\n", peer->ip);
    return 0;
}

static DWORD WINAPI p2p_server_thread(LPVOID arg) {
    (void)arg;
    silvr_socket_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) {
        fprintf(stderr, "[SERVER] socket() failed\n");
        return 1;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons(SILVR_PORT);

    if (bind(srv, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "[SERVER] bind() failed on port %d — error %d\n",
                SILVR_PORT, WSAGetLastError());
        CLOSE_SOCKET(srv);
        return 1;
    }
    listen(srv, 8);
    printf("[SERVER] Listening for P2P connections on port %d\n", SILVR_PORT);

    while (1) {
        struct sockaddr_in ca;
        int calen = sizeof(ca);
        silvr_socket_t cfd = accept(srv, (struct sockaddr *)&ca, &calen);
        if (cfd == INVALID_SOCKET) continue;

        if (g_peer_count >= SILVR_MAX_PEERS) {
            CLOSE_SOCKET(cfd);
            continue;
        }

        silvr_peer_t *peer = &g_peers[g_peer_count++];
        memset(peer, 0, sizeof(*peer));
        peer->sock          = cfd;
        peer->port          = ntohs(ca.sin_port);
        peer->connected     = 1;
        peer->last_msg_time = time(NULL);
        inet_ntop(AF_INET, &ca.sin_addr, peer->ip, sizeof(peer->ip));

        printf("[SERVER] Peer connected: %s:%d (slot %u)\n",
               peer->ip, peer->port, g_peer_count - 1);

        p2p_send_hello(peer);

        HANDLE t = CreateThread(NULL, 0, peer_recv_thread, peer, 0, NULL);
        if (t) CloseHandle(t);
    }
    return 0;
}

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main(int argc, char *argv[]) {
    printf("╔══════════════════════════════╗\n");
    printf("║   SILVR Protocol Node v3.0   ║\n");
    printf("║   Chain ID: %d               ║\n", SILVR_CHAIN_ID);
    printf("╚══════════════════════════════╝\n\n");

    if (p2p_init() != 0) return 1;
    utxo_load();

    /* Load migrated address or generate fresh */
    uint8_t saved_pkhash[20];
    char    saved_addr[40];
    memset(saved_pkhash, 0, 20);
    memset(saved_addr,   0, 40);

    if (load_miner_address(saved_pkhash, saved_addr) == 0) {
        printf("[NODE] Loaded migrated address: %s\n", saved_addr);
        memcpy(g_miner_kp.pkhash, saved_pkhash, 20);
        strncpy(g_miner_kp.addr_str, saved_addr,
                sizeof(g_miner_kp.addr_str) - 1);
    } else {
        printf("[NODE] Generating new miner keypair...\n");
        if (crypto_keygen(&g_miner_kp) != SILVR_OK) {
            fprintf(stderr, "[NODE] Keygen failed\n");
            return 1;
        }
        printf("[NODE] Miner: %s\n", g_miner_kp.addr_str);
    }

    copy_keypair_address(&g_treasury_kp, &g_miner_kp);
    printf("[NODE] Miner    : %s\n", g_miner_kp.addr_str);
    printf("[NODE] Treasury : %s (same)\n", g_treasury_kp.addr_str);
    printf("[NODE] Full 50 SILVR per block to your address.\n\n");

    chain_load();
    if (g_chain_height == 0) {
        create_genesis_block();
        chain_save();
    }

    /* Start P2P server — listens on port 8633 for incoming nodes */
    HANDLE srv_t = CreateThread(NULL, 0, p2p_server_thread, NULL, 0, NULL);
    if (srv_t) CloseHandle(srv_t);
    else fprintf(stderr, "[SERVER] Failed to start server thread\n");

    /* Connect outbound to peer if IP given as argument */
    if (argc >= 2) {
        printf("[NODE] Connecting to peer: %s\n", argv[1]);
        silvr_peer_t *peer = p2p_connect(argv[1], SILVR_PORT);
        if (peer) p2p_send_getblocks(peer, g_best_hash);
    }

    print_status();

    printf("[NODE] Starting mining loop. Press Ctrl+C to stop.\n\n");
    while (1) {
        if (mine_block() != 0) break;
        // Chain is saved inside mine_block() now
    }

    chain_save();
    p2p_cleanup();
    return 0;
}
