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
#define MINER_ADDRESS         "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE"
#define BLOCKCHAIN_FILE       "blockchain_v2.dat"
#define GENESIS_TOTAL         635191500000000ULL

static volatile int running = 1;

void handle_signal(int sig) {
    (void)sig;
    printf("\nSILVR node shutting down...\n");
    running = 0;
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
    uint8_t hash[32];
    uint64_t miner_reward;
    uint64_t treasury;
    uint64_t total_mined;
    char miner_address[64];
} silvr_saved_block_t;

void sha256d(const uint8_t *data, size_t len, uint8_t *out) {
    uint8_t tmp[32];
    SHA256(data, len, tmp);
    SHA256(tmp, 32, out);
}

uint64_t block_reward(uint64_t height) {
    uint64_t reward = SILVR_BLOCK_REWARD;
    uint64_t halvings = height / SILVR_HALVING;
    for (uint64_t i = 0; i < halvings && reward > 0; i++)
        reward /= 2;
    return reward;
}

int check_difficulty(uint8_t *hash, uint32_t difficulty) {
    uint32_t zeros = difficulty / 8;
    for (uint32_t i = 0; i < zeros && i < 32; i++)
        if (hash[i] != 0) return 0;
    return 1;
}

void print_hash(uint8_t *hash) {
    for (int i = 0; i < 32; i++)
        printf("%02x", hash[i]);
}

int save_block(silvr_saved_block_t *block) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "ab");
    if (!f) return 0;
    fwrite(block, sizeof(silvr_saved_block_t), 1, f);
    fclose(f);
    return 1;
}

int load_chain(uint64_t *height, uint64_t *total_mined,
               uint8_t *last_hash, uint32_t *difficulty) {
    FILE *f = fopen(BLOCKCHAIN_FILE, "rb");
    if (!f) return 0;
    silvr_saved_block_t block;
    uint64_t count = 0;
    silvr_saved_block_t last;
    while (fread(&block, sizeof(silvr_saved_block_t), 1, f) == 1) {
        last = block;
        count++;
    }
    fclose(f);
    if (count == 0) return 0;
    *height      = last.header.height + 1;
    *total_mined = last.total_mined;
    *difficulty  = last.header.difficulty;
    memcpy(last_hash, last.hash, 32);
    printf("Loaded %llu blocks. Resuming from block #%llu\n",
           (unsigned long long)count,
           (unsigned long long)*height);
    return 1;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    const char *miner_addr = MINER_ADDRESS;
    if (argc > 1) miner_addr = argv[1];

    printf("\n================================================\n");
    printf("  SILVR Node v2.0 - The People's Chain\n");
    printf("  Chain ID  : 2026\n");
    printf("  Miner     : %s\n", miner_addr);
    printf("  Max Supply: 42,000,000 SILVR\n");
    printf("================================================\n");
    printf("  GENESIS: 2,100,000 locked + 4,251,915 mined\n");
    printf("  TOTAL GENESIS: 6,351,915 SILVR\n");
    printf("================================================\n\n");

    uint8_t prev_hash[32];
    memset(prev_hash, 0, 32);
    uint64_t height      = 0;
    uint64_t total_mined = GENESIS_TOTAL;
    uint32_t difficulty  = 4;

    if (!load_chain(&height, &total_mined, prev_hash, &difficulty)) {
        printf("Starting fresh chain from genesis...\n\n");
    }

    silvr_block_header_t block;
    silvr_saved_block_t  saved;
    uint8_t current_hash[32];

    while (running) {
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

        uint64_t reward       = block_reward(height);
        uint64_t miner_reward = reward * 95 / 100;
        uint64_t treasury     = reward * 5 / 100;
        total_mined += miner_reward;

        memcpy(prev_hash, current_hash, 32);

        memset(&saved, 0, sizeof(saved));
        memcpy(&saved.header, &block, sizeof(block));
        memcpy(saved.hash, current_hash, 32);
        saved.miner_reward = miner_reward;
        saved.treasury     = treasury;
        saved.total_mined  = total_mined;
        strncpy(saved.miner_address, miner_addr, 63);
        save_block(&saved);

        printf("Block #%llu mined\n",
               (unsigned long long)(height + 1));
        printf("  Miner    : %s\n", miner_addr);
        printf("  Hash     : ");
        print_hash(current_hash);
        printf("\n");
        printf("  Reward   : %.8f SILVR\n",
               (double)miner_reward / 1e8);
        printf("  Treasury : %.8f SILVR\n",
               (double)treasury / 1e8);
        printf("  Total    : %.8f SILVR\n\n",
               (double)total_mined / 1e8);

        height++;

        if (height % 2016 == 0) {
            difficulty++;
            printf(">>> Difficulty adjusted to %u <<<\n\n",
                   difficulty);
        }
    }

    printf("Total mined: %.8f SILVR\n",
           (double)total_mined / 1e8);
    return 0;
}
