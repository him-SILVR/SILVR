#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define SILVR_MAX_SUPPLY     4200000000000000ULL
#define SILVR_INITIAL_REWARD 5000000000ULL
#define SILVR_HALVING        420000
#define SILVR_TREASURY_PCT   5
#define SILVR_BURN_BPS       100
#define SILVR_BLOCK_TIME     300
#define SILVR_PORT           8633
#define SILVR_CHAIN_ID       2026

uint64_t block_reward(uint32_t height) {
    uint64_t reward = SILVR_INITIAL_REWARD;
    uint32_t halvings = height / SILVR_HALVING;
    if (halvings >= 64) return 0;
    return reward >> halvings;
}

uint64_t treasury_cut(uint32_t height) {
    return block_reward(height) * SILVR_TREASURY_PCT / 100;
}

uint64_t miner_reward(uint32_t height) {
    return block_reward(height) - treasury_cut(height);
}

void mine_block(uint32_t height) {
    uint64_t reward = miner_reward(height);
    uint64_t treasury = treasury_cut(height);
    printf("Block %u mined\n", height);
    printf("  Miner reward : %llu grains (%.8f SILVR)\n",
           reward, (double)reward / 1e8);
    printf("  Treasury     : %llu grains (%.8f SILVR)\n",
           treasury, (double)treasury / 1e8);
}

int main() {
    printf("\n");
    printf("================================\n");
    printf("  SILVR Node Starting\n");
    printf("  Chain ID : %d\n", SILVR_CHAIN_ID);
    printf("  Port     : %d\n", SILVR_PORT);
    printf("  Genesis  : The People's Chain\n");
    printf("================================\n\n");

    printf("Mining started...\n\n");

    uint32_t height = 0;
    while (1) {
        mine_block(height);
        height++;
        if (height % 10 == 0) {
            printf("\n--- %u blocks mined ---\n\n", height);
        }
    }
    return 0;
}
