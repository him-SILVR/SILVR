#include "../../include/silvr.h"
#include <signal.h>
#include <unistd.h>

static volatile int running = 1;

void handle_signal(int sig) {
    printf("\nSILVR node shutting down...\n");
    running = 0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("\n");
    printf("================================================\n");
    printf("  SILVR Node v1.0 - Production\n");
    printf("  Chain ID    : %d\n", SILVR_CHAIN_ID);
    printf("  Port        : %d\n", SILVR_PORT);
    printf("  Max Supply  : %llu SILVR\n",
           (unsigned long long)SILVR_MAX_SUPPLY);
    printf("  Block Time  : %d seconds\n", SILVR_BLOCK_TIME);
    printf("  Genesis     : The People's Chain\n");
    printf("================================================\n\n");

    /* Create miner wallet */
    silvr_wallet_t miner_wallet;

    if (argc > 1) {
        /* Load from private key hex argument */
        uint8_t privkey[32];
        const char *hex = argv[1];
        for (int i = 0; i < 32; i++) {
            unsigned int byte;
            sscanf(hex + 2*i, "%02x", &byte);
            privkey[i] = (uint8_t)byte;
        }
        silvr_wallet_from_privkey(&miner_wallet, privkey);
        printf("Loaded existing wallet\n");
    } else {
    /* Use hardcoded miner address */
    strncpy(miner_wallet.address,
        "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE",
        sizeof(miner_wallet.address)-1);
    printf("Using miner address\n");
}

    }

    silvr_wallet_print(&miner_wallet);

    /* Mining loop */
    silvr_block_t block;
    memset(&block, 0, sizeof(block));

    silvr_height_t height = 0;
    uint64_t total_mined = 0;
    uint32_t difficulty = 1;

    printf("Starting mining...\n\n");

    while (running) {
        memset(&block, 0, sizeof(block));
        block.header.height = height;
        block.header.version = 1;

        /* Mine the block */
        silvr_mine_block(&block, difficulty);

        uint64_t reward = silvr_miner_reward(height);
        uint64_t treasury = silvr_treasury_cut(height);
        total_mined += reward;

        /* Print block info */
        printf("Block #%u mined\n", height + 1);
        printf("  Miner    : %s\n", miner_wallet.address);
        printf("  Reward   : %.8f SILVR\n",
               (double)reward / 1e8);
        printf("  Treasury : %.8f SILVR\n",
               (double)treasury / 1e8);
        printf("  Total    : %.8f SILVR\n\n",
               (double)total_mined / 1e8);

        height++;

        /* Adjust difficulty every 2016 blocks */
        if (height % 2016 == 0) {
            difficulty++;
            printf("Difficulty adjusted to %u\n\n", difficulty);
        }

        sleep(1);
    }

    printf("Total mined: %.8f SILVR\n",
           (double)total_mined / 1e8);
    return 0;
}
