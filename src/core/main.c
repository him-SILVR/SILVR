#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define SILVR_MAX_SUPPLY     4200000000000000ULL
#define SILVR_INITIAL_REWARD 5000000000ULL
#define SILVR_HALVING        420000
#define SILVR_TREASURY_PCT   5
#define SILVR_BLOCK_TIME     300
#define SILVR_PORT           8633
#define SILVR_CHAIN_ID       2026

/* YOUR WALLET ADDRESS GOES HERE */
#define MINER_ADDRESS "b602a8cf49b570cc815b0841474fe3c547a36fd3acfd00aca5afe3d51faa7077"

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

void mine_block(uint32_t height, uint64_t *total) {
    uint64_t reward = miner_reward(height);
    uint64_t treasury = treasury_cut(height);
    *total += reward;
    printf("Block %u mined\n", height);
    printf("  Address      : %s\n", MINER_ADDRESS);
    printf("  Miner reward : %.8f SILVR\n", (double)reward / 1e8);
    printf("  Treasury     : %.8f SILVR\n", (double)treasury / 1e8);
    printf("  Total mined  : %.8f SILVR\n\n", (double)*total / 1e8);
}

int main() {
    printf("\n");
    printf("================================\n");
    printf("  SILVR Node v1.0\n");
    printf("  Chain ID : %d\n", SILVR_CHAIN_ID);
    printf("  Port     : %d\n", SILVR_PORT);
    printf("  Miner    : %s\n", MINER_ADDRESS);
    printf("  Genesis  : The People's Chain\n");
    printf("================================\n\n");

    uint32_t height = 0;
    uint64_t total = 0;

    while (1) {
        mine_block(height, &total);
        height++;
        sleep(1);
    }
    return 0;
}
