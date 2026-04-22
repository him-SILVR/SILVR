cat > /e/GitHub/SILVR/silvrd_v2.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define SILVR_CHAIN_ID        2026
#define SILVR_BLOCK_REWARD    5000000000ULL
#define SILVR_HALVING         420000
#define SILVR_MAX_SUPPLY      4200000000000000ULL
#define MINER_ADDRESS         "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE"
#define BLOCKCHAIN_FILE       "blockchain_v2.dat"
#define UTXO_FILE             "utxo.dat"
#define TX_FILE               "mempool.dat"
#define GENESIS_TOTAL         635191500000000ULL
#define MAX_ADDRESSES         10000
#define ADDR_LEN              64
#define MAX_MEMPOOL           1000

static volatile int running = 1;

/* ── UTXO ── */
typedef struct {
    char     address[ADDR_LEN];
    uint64_t balance;
    uint64_t total_received;
    uint64_t total_sent;
    uint64_t block_count;
} utxo_entry_t;

typedef struct {
    utxo_entry_t entries[MAX_ADDRESSES];
    uint32_t     count;
    uint64_t     total_supply;
    uint64_t     last_block;
} utxo_db_t;

static utxo_db_t utxo;

void utxo_init(void) { memset(&utxo, 0, sizeof(utxo)); }

int utxo_load(void) {
    FILE *f = fopen(UTXO_FILE, "rb");
    if (!f) return 0;
    fread(&utxo, sizeof(utxo_db_t), 1, f);
    fclose(f);
    printf("[UTXO] Loaded %u addresses. Supply: %.8f SILVR\n",
           utxo.count, (double)utxo.total_supply / 1e8);
    return 1;
}

void utxo_save(void) {
    FILE *f = fopen(UTXO_FILE, "wb");
    if (!f) return;
    fwrite(&utxo, sizeof(utxo_db_t), 1, f);
    fclose(f);
}

utxo_entry_t *utxo_find(const char *addr) {
    for (uint32_t i = 0; i < utxo.count; i++)
        if (strncmp(utxo.entries[i].address, addr, ADDR_LEN) == 0)
            return &utxo.entries[i];
    return NULL;
}

utxo_entry_t *utxo_get(const char *addr) {
    utxo_entry_t *e = utxo_find(addr);
    if (e) return e;
    if (utxo.count >= MAX_ADDRESSES) return NULL;
    e = &utxo.entries[utxo.count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->address, addr, ADDR_LEN - 1);
    return e;
}

void utxo_credit(const char *addr, uint64_t amount, uint64_t height) {
    utxo_entry_t *e = utxo_get(addr);
    if (!e) return;
    e->balance        += amount;
    e->total_received += amount;
    e->block_count++;
    utxo.total_supply += amount;
    utxo.last_block    = height;
}

uint64_t utxo_balance(const char *addr) {
    utxo_entry_t *e = utxo_find(addr);
    return e ? e->balance : 0;
}

void utxo_print(const char *addr) {
    utxo_entry_t *e = utxo_find(addr);
    if (!e) { printf("[UTXO] Not found: %s\n", addr); return; }
    printf("\n========================================\n");
    printf("  Address  : %s\n", e->address);
    printf("  Balance  : %.8f SILVR\n", (double)e->balance / 1e8);
    printf("  Received : %.8f SILVR\n", (double)e->total_received / 1e8);
    printf("  Sent     : %.8f SILVR\n", (double)e->total_sent / 1e8);
    printf("  Blocks   : %llu\n", (unsigned long long)e->block_count);
    printf("========================================\n\n");
}

void utxo_stats(void) {
    printf("\n========================================\n");
    printf("  UTXO STATS\n");
    printf("  Addresses : %u\n", utxo.count);
    printf("  Supply    : %.8f SILVR\n",
           (double)utxo.total_supply / 1e8);
    printf("  Last Block: %llu\n",
           (unsigned long long)utxo.last_block);
    printf("========================================\n\n");
}

/* ── TRANSACTION ── */
typedef struct {
    uint8_t  txid[32];
    char     from[ADDR_LEN];
    char     to[ADDR_LEN];
    uint64_t amount;
    uint64_t fee;
    uint32_t timestamp;
    uint64_t nonce;
    uint8_t  signature[64];
    uint8_t  valid;
} silvr_tx_t;

/* Simple transaction signing using SHA256d of tx data as proof */
void tx_compute_txid(silvr_tx_t *tx) {
    uint8_t buf[256];
    size_t  off = 0;
    memcpy(buf + off, tx->from,      ADDR_LEN); off += ADDR_LEN;
    memcpy(buf + off, tx->to,        ADDR_LEN); off += ADDR_LEN;
    memcpy(buf + off, &tx->amount,   8);         off += 8;
    memcpy(buf + off, &tx->fee,      8);         off += 8;
    memcpy(buf + off, &tx->timestamp,4);         off += 4;
    memcpy(buf + off, &tx->nonce,    8);         off += 8;
    uint8_t tmp[32];
    SHA256(buf, off, tmp);
    SHA256(tmp, 32, tx->txid);
}

void tx_sign(silvr_tx_t *tx) {
    /* Sign = SHA256d(txid + nonce) stored in signature field */
    uint8_t buf[40];
    memcpy(buf,      tx->txid,   32);
    memcpy(buf + 32, &tx->nonce,  8);
    uint8_t tmp[32];
    SHA256(buf, 40, tmp);
    SHA256(tmp, 32, tx->signature);
    /* Pad remaining 32 bytes with txid for verification */
    memcpy(tx->signature + 32, tx->txid, 32);
    tx->valid = 1;
}

int tx_verify(silvr_tx_t *tx) {
    if (!tx->valid) return 0;
    uint8_t buf[40];
    memcpy(buf,      tx->txid,   32);
    memcpy(buf + 32, &tx->nonce,  8);
    uint8_t tmp[32], expected[32];
    SHA256(buf, 40, tmp);
    SHA256(tmp, 32, expected);
    if (memcmp(expected, tx->signature, 32) != 0) return 0;
    if (memcmp(tx->txid, tx->signature + 32, 32) != 0) return 0;
    return 1;
}

int tx_validate(silvr_tx_t *tx) {
    if (!tx_verify(tx)) {
        printf("[TX] Invalid signature\n");
        return 0;
    }
    uint64_t bal = utxo_balance(tx->from);
    if (bal < tx->amount + tx->fee) {
        printf("[TX] Insufficient balance. Have %.8f need %.8f\n",
               (double)bal / 1e8,
               (double)(tx->amount + tx->fee) / 1e8);
        return 0;
    }
    return 1;
}

int tx_execute(silvr_tx_t *tx) {
    if (!tx_validate(tx)) return 0;
    utxo_entry_t *s = utxo_find(tx->from);
    if (!s) return 0;
    s->balance    -= (tx->amount + tx->fee);
    s->total_sent += (tx->amount + tx->fee);
    utxo_entry_t *r = utxo_get(tx->to);
    if (!r) return 0;
    r->balance        += tx->amount;
    r->total_received += tx->amount;
    printf("[TX] Sent %.8f SILVR\n", (double)tx->amount / 1e8);
    printf("     From : %s\n", tx->from);
    printf("     To   : %s\n", tx->to);
    printf("     Fee  : %.8f SILVR\n", (double)tx->fee / 1e8);
    printf("     TXID : ");
    for (int i = 0; i < 32; i++) printf("%02x", tx->txid[i]);
    printf("\n\n");
    utxo_save();
    return 1;
}

silvr_tx_t tx_create(const char *from, const char *to,
                     uint64_t amount, uint64_t fee) {
    silvr_tx_t tx;
    memset(&tx, 0, sizeof(tx));
    strncpy(tx.from, from, ADDR_LEN - 1);
    strncpy(tx.to,   to,   ADDR_LEN - 1);
    tx.amount    = amount;
    tx.fee       = fee;
    tx.timestamp = (uint32_t)time(NULL);
    tx.nonce     = (uint64_t)time(NULL) ^ (uint64_t)rand();
    tx_compute_txid(&tx);
    tx_sign(&tx);
    return tx;
}

/* ── BLOCK ── */
typedef struct {
    uint32_t version;
    uint8_t  prev_hash[32];
    uint8_t  merkle_root[32];
    uint32_t timestamp;
    uint32_t difficulty;
    uint64_t nonce;
    uint64_t height;
} silvr_block_header_t;

typedef struct {
    silvr_block_header_t header;
    uint8_t  hash[32];
    uint64_t miner_reward;
    uint64_t treasury;
    uint64_t total_mined;
    char     miner_address[64];
} silvr_saved_block_t;

void sha256d(const uint8_t *data, size_t len, uint8_t *out) {
    uint8_t tmp[32];
    SHA256(data, len, tmp);
    SHA256(tmp, 32, out);
}

void print_hash(uint8_t *hash) {
    for (int i = 0; i < 32; i++) printf("%02x", hash[i]);
}

uint64_t block_reward(uint64_t height) {
    uint64_t r = SILVR_BLOCK_REWARD;
    for (uint64_t i = 0; i < height / SILVR_HALVING && r > 0; i++)
        r /= 2;
    return r;
}

int check_difficulty(uint8_t *hash, uint32_t bits) {
    for (uint32_t i = 0; i < bits && i < 256; i++) {
        uint32_t byte = i / 8;
        uint32_t bit  = 7 - (i % 8);
        if ((hash[byte] >> bit) & 1) return 0;
    }
    return 1;
}

int save_block(silvr_saved_block_t *b) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "ab");
    if (!f) return 0;
    fwrite(b, sizeof(*b), 1, f);
    fclose(f);
    return 1;
}

int load_chain(uint64_t *height, uint64_t *total,
               uint8_t *last_hash, uint32_t *diff) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "rb");
    if (!f) return 0;
    silvr_saved_block_t b, last;
    uint64_t n = 0;
    while (fread(&b, sizeof(b), 1, f) == 1) { last = b; n++; }
    fclose(f);
    if (!n) return 0;
    *height = last.header.height + 1;
    *total  = last.total_mined;
    *diff   = last.header.difficulty;
    memcpy(last_hash, last.hash, 32);
    printf("[CHAIN] Loaded %llu blocks. Resuming from #%llu\n",
           (unsigned long long)n,
           (unsigned long long)*height);
    return 1;
}

void handle_signal(int sig) {
    (void)sig;
    printf("\nSILVR node shutting down...\n");
    running = 0;
}

/* ── DEMO TX on startup ── */
void demo_transaction(const char *from) {
    printf("\n[TX DEMO] Creating test transaction...\n");
    const char *test_to = "STest1ReceiverAddressDemo000000000";
    uint64_t amount = 100000000ULL; /* 1.0 SILVR */
    uint64_t fee    =   1000000ULL; /* 0.01 SILVR */

    if (utxo_balance(from) < amount + fee) {
        printf("[TX DEMO] Skipped — insufficient balance\n\n");
        return;
    }

    silvr_tx_t tx = tx_create(from, test_to, amount, fee);

    printf("[TX DEMO] Transaction created:\n");
    printf("  From   : %s\n", tx.from);
    printf("  To     : %s\n", tx.to);
    printf("  Amount : %.8f SILVR\n", (double)tx.amount / 1e8);
    printf("  Fee    : %.8f SILVR\n", (double)tx.fee / 1e8);
    printf("  TXID   : ");
    for (int i = 0; i < 32; i++) printf("%02x", tx.txid[i]);
    printf("\n");
    printf("  Valid  : %s\n\n", tx_verify(&tx) ? "YES" : "NO");

    if (tx_execute(&tx)) {
        printf("[TX DEMO] Transaction executed successfully!\n\n");
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    const char *miner = MINER_ADDRESS;
    if (argc > 1) miner = argv[1];

    printf("\n================================================\n");
    printf("  SILVR Node v2.2 - The People's Chain\n");
    printf("  Chain ID  : %d\n", SILVR_CHAIN_ID);
    printf("  Miner     : %s\n", miner);
    printf("  Max Supply: 42,000,000 SILVR\n");
    printf("================================================\n");
    printf("  GENESIS: 2,100,000 locked + 4,251,915 mined\n");
    printf("  TOTAL GENESIS: 6,351,915 SILVR\n");
    printf("================================================\n\n");

    utxo_init();
    if (!utxo_load()) {
        printf("[UTXO] Fresh database. Seeding genesis...\n");
        utxo_credit(MINER_ADDRESS, GENESIS_TOTAL, 0);
        utxo_save();
    }

    uint8_t  prev_hash[32];
    memset(prev_hash, 0, 32);
    uint64_t height      = 0;
    uint64_t total_mined = GENESIS_TOTAL;
    uint32_t difficulty  = 20;

    if (!load_chain(&height, &total_mined, prev_hash, &difficulty))
        printf("[CHAIN] Starting fresh from genesis...\n\n");

    utxo_print(miner);

    /* Demo transaction on startup */
    demo_transaction(miner);

    silvr_block_header_t block;
    silvr_saved_block_t  saved;
    uint8_t current_hash[32];

    printf("Mining...\n\n");

    while (running) {
        if (total_mined >= SILVR_MAX_SUPPLY) {
            printf("Max supply reached.\n");
            break;
        }

        memset(&block, 0, sizeof(block));
        block.version    = 1;
        block.height     = height;
        block.timestamp  = (uint32_t)time(NULL);
        block.difficulty = difficulty;
        block.nonce      = 0;
        memcpy(block.prev_hash, prev_hash, 32);

        do {
            block.nonce++;
            sha256d((uint8_t*)&block, sizeof(block), current_hash);
        } while (!check_difficulty(current_hash, difficulty) && running);

        if (!running) break;

        uint64_t reward  = block_reward(height);
        uint64_t mreward = reward * 95 / 100;
        uint64_t treas   = reward *  5 / 100;

        if (total_mined + mreward > SILVR_MAX_SUPPLY)
            mreward = SILVR_MAX_SUPPLY - total_mined;

        total_mined += mreward;
        memcpy(prev_hash, current_hash, 32);

        utxo_credit(miner, mreward, height);
        utxo_save();

        memset(&saved, 0, sizeof(saved));
        memcpy(&saved.header, &block, sizeof(block));
        memcpy(saved.hash, current_hash, 32);
        saved.miner_reward = mreward;
        saved.treasury     = treas;
        saved.total_mined  = total_mined;
        strncpy(saved.miner_address, miner, 63);
        save_block(&saved);

        printf("Block #%llu\n",
               (unsigned long long)(height + 1));
        printf("  Hash    : ");
        print_hash(current_hash);
        printf("\n");
        printf("  Reward  : %.8f SILVR\n", (double)mreward / 1e8);
        printf("  Balance : %.8f SILVR\n",
               (double)utxo_balance(miner) / 1e8);
        printf("  Supply  : %.8f / 42,000,000\n\n",
               (double)total_mined / 1e8);

        height++;

        if (height % 2016 == 0) {
            difficulty++;
            printf(">>> Difficulty -> %u bits <<<\n\n", difficulty);
        }

        if (height % 100 == 0) utxo_stats();
    }

    printf("\nFinal state:\n");
    utxo_print(miner);
    utxo_stats();
    return 0;
}
EOF
