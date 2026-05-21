/*
 * silvrd_v3.c — SILVR Protocol Node v3.0 FINAL
 * ==============================================
 * Real ECDSA signing + True UTXO + P2P sync
 * Miner and Treasury both use your original address.
 * All 50 SILVR per block goes to SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE
 *
 * SILVR Chain Parameters:
 *   Chain ID:    2026  |  Ticker: SILVR  |  Max: 42,000,000
 *   Reward:      50 SILVR (47.5 miner + 2.5 treasury = same address)
 *   Block time:  5 minutes  |  Halving: every 420,000 blocks
 *   Port:        8633  |  PoW: SHA-256d 25-bit
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
#define MAX_CHAIN_BLOCKS        1000000

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
 * MINE ONE BLOCK
 * ========================================================================= */
static int mine_block(void) {
    if (g_chain_height >= MAX_CHAIN_BLOCKS) return -1;

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
    printf("... nonce=%u reward=%.2f SILVR\n",
           block->header.nonce,
           (double)(miner_reward + treasury_reward) / SILVR_SATOSHIS);

    utxo_save();
    return 0;
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

static void chain_load(void) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "rb");
    if (!f) {
        printf("[NODE] No existing chain — starting fresh\n");
        return;
    }
    fread(&g_chain_height, sizeof(g_chain_height), 1, f);
    fread(g_chain, sizeof(silvr_block_t), g_chain_height, f);
    fread(g_block_hashes, 32, g_chain_height, f);
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
        /* Use your original v2.4 address */
        printf("[NODE] Loaded migrated address: %s\n", saved_addr);
        memcpy(g_miner_kp.pkhash, saved_pkhash, 20);
        strncpy(g_miner_kp.addr_str, saved_addr,
                sizeof(g_miner_kp.addr_str) - 1);
    } else {
        /* No migration file — generate fresh keypair */
        printf("[NODE] Generating new miner keypair...\n");
        if (crypto_keygen(&g_miner_kp) != SILVR_OK) {
            fprintf(stderr, "[NODE] Keygen failed\n");
            return 1;
        }
        printf("[NODE] Miner: %s\n", g_miner_kp.addr_str);
    }

    /* Treasury = Miner. Full 50 SILVR per block to one address. */
    copy_keypair_address(&g_treasury_kp, &g_miner_kp);
    printf("[NODE] Miner    : %s\n", g_miner_kp.addr_str);
    printf("[NODE] Treasury : %s (same)\n", g_treasury_kp.addr_str);
    printf("[NODE] Full 50 SILVR per block to your address.\n\n");

    /* Load or create chain */
    chain_load();
    if (g_chain_height == 0) {
        create_genesis_block();
        chain_save();
    }

    /* Connect to peer if IP provided as argument */
    if (argc >= 2) {
        printf("[NODE] Connecting to peer: %s\n", argv[1]);
        silvr_peer_t *peer = p2p_connect(argv[1], SILVR_PORT);
        if (peer) p2p_send_getblocks(peer, g_best_hash);
    }

    print_status();

    printf("[NODE] Starting mining loop. Press Ctrl+C to stop.\n\n");
    uint64_t blocks = 0;
    while (1) {
        if (mine_block() != 0) break;
        blocks++;
        if (blocks % 10  == 0) print_status();
        if (blocks % 100 == 0) chain_save();
    }

    chain_save();
    p2p_cleanup();
    return 0;
}
