#include "../../include/silvr.h"
#include <openssl/sha.h>
#include <time.h>

uint64_t silvr_block_reward(silvr_height_t height) {
    uint64_t reward = SILVR_INITIAL_REWARD;
    uint32_t halvings = height / SILVR_HALVING_INTERVAL;
    if (halvings >= 64) return 0;
    return reward >> halvings;
}

uint64_t silvr_treasury_cut(silvr_height_t height) {
    return silvr_block_reward(height) * SILVR_TREASURY_PCT / 100;
}

uint64_t silvr_miner_reward(silvr_height_t height) {
    return silvr_block_reward(height) - silvr_treasury_cut(height);
}

static void compute_block_hash(silvr_block_header_t *header,
                                uint8_t *out) {
    uint8_t tmp[32];
    SHA256((uint8_t*)header, sizeof(silvr_block_header_t), tmp);
    SHA256(tmp, 32, out);
}

static int check_difficulty(const uint8_t *hash,
                             uint32_t difficulty) {
    uint32_t required_zeros = difficulty / 8;
    for (uint32_t i = 0; i < required_zeros && i < 32; i++) {
        if (hash[i] != 0) return 0;
    }
    return 1;
}

int silvr_mine_block(silvr_block_t *block, uint32_t difficulty) {
    block->header.timestamp = (uint32_t)time(NULL);
    block->header.bits = difficulty;
    block->header.nonce = 0;

    uint8_t hash[32];
    uint64_t attempts = 0;

    while (1) {
        compute_block_hash(&block->header, hash);
        if (check_difficulty(hash, difficulty)) {
            memcpy(block->hash, hash, 32);
            return 0;
        }
        block->header.nonce++;
        attempts++;
        if (block->header.nonce == 0) {
            block->header.timestamp = (uint32_t)time(NULL);
        }
    }
}

void silvr_block_print(const silvr_block_t *block) {
    printf("================================\n");
    printf("Block #%u\n", block->header.height);
    printf("  Time     : %u\n", block->header.timestamp);
    printf("  Nonce    : %llu\n",
           (unsigned long long)block->header.nonce);
    printf("  Hash     : ");
    for (int i = 0; i < 32; i++)
        printf("%02x", block->hash[i]);
    printf("\n");
    printf("  Reward   : %.8f SILVR\n",
           (double)silvr_miner_reward(block->header.height) / 1e8);
    printf("  Treasury : %.8f SILVR\n",
           (double)silvr_treasury_cut(block->header.height) / 1e8);
    printf("================================\n\n");
}
