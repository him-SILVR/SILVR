cat > /e/GitHub/SILVR/silvrd_v2.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <openssl/sha.h>

#define SILVR_CHAIN_ID        2026
#define SILVR_BLOCK_REWARD    5000000000ULL
#define SILVR_HALVING         420000
#define SILVR_MAX_SUPPLY      4200000000000000ULL
#define MINER_ADDRESS         "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE"
#define BLOCKCHAIN_FILE       "blockchain_v2.dat"
#define UTXO_FILE             "utxo.dat"
#define GENESIS_TOTAL         635191500000000ULL
#define MAX_ADDRESSES         10000
#define ADDR_LEN              64

static volatile int running = 1;
static uint64_t total_hashes = 0;

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
    return 1;
}

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void clear_line(void) { printf("\r\033[K"); }

void print_banner(void) {
    printf("\033[2J\033[H");
    printf("  +---------------------------------------------------------+\n");
    printf("  |         SSSS  III  L    V   V  RRRR    Chain ID 2026   |\n");
    printf("  |        S       I   L    V   V  R   R                   |\n");
    printf("  |         SSS    I   L     V V   RRRR    SHA-256d PoW    |\n");
    printf("  |            S   I   L     V V   R R     The People's    |\n");
    printf("  |        SSSS   III  LLLL   V    R  RR   Chain           |\n");
    printf("  +---------------------------------------------------------+\n\n");
}

void print_separator(void) {
    printf("  +----------------------------------+--------------------+\n");
}

void print_mining_status(uint64_t height, uint32_t diff,
                          uint64_t nonce, uint64_t balance,
                          uint64_t total, time_t start) {
    time_t now = time(NULL);
    double elapsed = difftime(now, start);
    double hashrate = elapsed > 0 ? (double)total_hashes / elapsed : 0;
    char hr_buf[32];
    if (hashrate > 1000000)
        snprintf(hr_buf, sizeof(hr_buf), "%.2f MH/s", hashrate/1000000);
    else if (hashrate > 1000)
        snprintf(hr_buf, sizeof(hr_buf), "%.2f KH/s", hashrate/1000);
    else
        snprintf(hr_buf, sizeof(hr_buf), "%.0f H/s", hashrate);

    double pct = (double)total / (double)SILVR_MAX_SUPPLY * 100.0;

    printf("\r\033[K");
    printf("  Mining block #%-8llu  Difficulty: %u bits  Nonce: %-12llu  %s",
           (unsigned long long)(height + 1),
           diff,
           (unsigned long long)nonce,
           hr_buf);
    fflush(stdout);
}

void print_block_found(uint64_t height, uint8_t *hash,
                        uint64_t reward, uint64_t balance,
                        uint64_t total, uint64_t nonce,
                        uint32_t diff, time_t block_time) {
    char timebuf[32];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

    double supply_pct = (double)total / (double)SILVR_MAX_SUPPLY * 100.0;

    printf("\n");
    print_separator();
    printf("  | BLOCK #%-28llu %s |\n",
           (unsigned long long)(height + 1), timebuf);
    print_separator();
    printf("  | Hash     ");
    for (int i = 0; i < 16; i++) printf("%02x", hash[i]);
    printf("... |\n");
    printf("  | Nonce    %-38llu |\n", (unsigned long long)nonce);
    printf("  | Reward   %-29.8f SILVR |\n", (double)reward / 1e8);
    printf("  | Balance  %-29.8f SILVR |\n", (double)balance / 1e8);
    printf("  | Supply   %-24.2f / 42,000,000 |\n", (double)total / 1e8);
    printf("  | Mined    %-33.4f%% |\n", supply_pct);
    print_separator();
    printf("\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    const char *miner = MINER_ADDRESS;
    if (argc > 1) miner = argv[1];

    print_banner();

    utxo_init();
    if (!utxo_load()) {
        utxo_credit(MINER_ADDRESS, GENESIS_TOTAL, 0);
        utxo_save();
    }

    uint8_t  prev_hash[32];
    memset(prev_hash, 0, 32);
    uint64_t height      = 0;
    uint64_t total_mined = GENESIS_TOTAL;
    uint32_t difficulty  = 20;

    int resumed = load_chain(&height, &total_mined, prev_hash, &difficulty);

    printf("  Miner   : %s\n", miner);
    printf("  Balance : %.8f SILVR\n",
           (double)utxo_balance(miner) / 1e8);
    printf("  Blocks  : %llu\n", (unsigned long long)height);
    printf("  Status  : %s\n\n",
           resumed ? "Resumed from saved chain" : "Starting fresh chain");
    printf("  Starting mining...\n\n");

    silvr_block_header_t block;
    silvr_saved_block_t  saved;
    uint8_t current_hash[32];
    time_t  session_start = time(NULL);

    while (running) {
        if (total_mined >= SILVR_MAX_SUPPLY) {
            printf("\n  Max supply reached. Mining complete.\n");
            break;
        }

        memset(&block, 0, sizeof(block));
        block.version    = 1;
        block.height     = height;
        block.timestamp  = (uint32_t)time(NULL);
        block.difficulty = difficulty;
        block.nonce      = 0;
        memcpy(block.prev_hash, prev_hash, 32);

        uint64_t last_print = 0;
        do {
            block.nonce++;
            total_hashes++;
            sha256d((uint8_t*)&block, sizeof(block), current_hash);
            if (block.nonce - last_print > 50000) {
                print_mining_status(height, difficulty, block.nonce,
                                    utxo_balance(miner),
                                    total_mined, session_start);
                last_print = block.nonce;
            }
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

        print_block_found(height, current_hash, mreward,
                          utxo_balance(miner), total_mined,
                          block.nonce, difficulty,
                          (time_t)block.timestamp);

        height++;

        if (height % 2016 == 0) {
            difficulty++;
            printf("  >>> Difficulty adjusted to %u bits <<<\n\n",
                   difficulty);
        }
    }

    printf("\n  Node stopped.\n");
    printf("  Final balance : %.8f SILVR\n",
           (double)utxo_balance(miner) / 1e8);
    printf("  Total blocks  : %llu\n\n",
           (unsigned long long)height);
    return 0;
}
EOF
