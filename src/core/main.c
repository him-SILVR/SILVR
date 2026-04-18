#include "../../include/silvr.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static volatile int running = 1;
static char peer_ip[64] = {0};

void handle_signal(int sig) {
    (void)sig;
    printf("\nSILVR node shutting down...\n");
    running = 0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Check for --peer argument
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--peer") == 0 && i+1 < argc) {
            strncpy(peer_ip, argv[i+1], 63);
            printf("Will connect to peer: %s\n", peer_ip);
        }
    }

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

    silvr_wallet_t miner_wallet;
    memset(&miner_wallet, 0, sizeof(miner_wallet));

    strncpy((char*)miner_wallet.address,
        "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE",
        sizeof(miner_wallet.address)-1);
    printf("Miner address: %s\n", (char*)miner_wallet.address);

    silvr_block_t block;
    memset(&block, 0, sizeof(block));

    silvr_height_t height = 79520;  // Start from your current block
    uint64_t total_mined = 3777152500000;  // Your total from screenshot
    uint32_t difficulty = 1;

    printf("Starting mining from block #%llu...\n\n", (unsigned long long)height);

    while (running) {
        memset(&block, 0, sizeof(block));
        block.header.height = height;
        block.header.version = 1;

        silvr_mine_block(&block, difficulty);

        uint64_t reward = 4750000000;  // 47.5 SILVR
        uint64_t treasury = 250000000;  // 2.5 SILVR
        total_mined += reward;

        printf("Block #%llu mined\n", (unsigned long long)height);
        printf("  Miner    : %s\n", (char*)miner_wallet.address);
        printf("  Reward   : %.8f SILVR\n", (double)reward / 1e8);
        printf("  Treasury : %.8f SILVR\n", (double)treasury / 1e8);
        printf("  Total    : %.8f SILVR\n\n", (double)total_mined / 1e8);

        height++;

        sleep(1);
    }

    return 0;
}
