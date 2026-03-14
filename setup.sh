#!/bin/bash

echo "================================================"
echo "  SILVR Production Setup"
echo "  Building real blockchain from scratch"
echo "================================================"

# Install dependencies
echo "Installing crypto libraries..."
sudo apt-get update -qq
sudo apt-get install -y libsecp256k1-dev libssl-dev build-essential 2>/dev/null

# Create directory structure
echo "Creating project structure..."
mkdir -p src/consensus
mkdir -p src/wallet
mkdir -p src/network
mkdir -p src/dao
mkdir -p src/core
mkdir -p include
mkdir -p data

# Create master header
echo "Creating silvr.h..."
cat > include/silvr.h << 'EOF'
#ifndef SILVR_H
#define SILVR_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Chain constants */
#define SILVR_CHAIN_ID           2026
#define SILVR_PORT               8633
#define SILVR_MAX_SUPPLY         42000000ULL
#define SILVR_INITIAL_REWARD     5000000000ULL
#define SILVR_HALVING_INTERVAL   420000
#define SILVR_TREASURY_PCT       5
#define SILVR_BURN_BPS           100
#define SILVR_BLOCK_TIME         300
#define SILVR_MIN_STAKE          100000000000ULL
#define SILVR_VALIDATOR_STAKE    1000000000000ULL
#define SILVR_ADDRESS_SIZE       35
#define SILVR_HASH_SIZE          32
#define SILVR_PUBKEY_SIZE        33
#define SILVR_SIG_SIZE           64
#define SILVR_VERSION_BYTE       0x3F

/* Types */
typedef uint8_t  silvr_hash_t[32];
typedef uint8_t  silvr_pubkey_t[33];
typedef uint8_t  silvr_sig_t[64];
typedef uint32_t silvr_height_t;
typedef uint64_t silvr_amount_t;
typedef char     silvr_address_t[36];

/* Block header */
typedef struct {
    uint32_t        version;
    silvr_hash_t    prev_hash;
    silvr_hash_t    merkle_root;
    uint32_t        timestamp;
    uint32_t        bits;
    uint64_t        nonce;
    silvr_height_t  height;
} silvr_block_header_t;

/* Transaction output */
typedef struct {
    silvr_amount_t  value;
    silvr_address_t address;
} silvr_txout_t;

/* Transaction input */
typedef struct {
    silvr_hash_t    prev_tx;
    uint32_t        prev_index;
    silvr_sig_t     signature;
    silvr_pubkey_t  pubkey;
} silvr_txin_t;

/* Transaction */
typedef struct {
    uint32_t        version;
    uint32_t        num_inputs;
    silvr_txin_t    inputs[16];
    uint32_t        num_outputs;
    silvr_txout_t   outputs[16];
    uint32_t        locktime;
    silvr_hash_t    txid;
} silvr_tx_t;

/* Block */
typedef struct {
    silvr_block_header_t header;
    uint32_t             num_txs;
    silvr_tx_t           txs[512];
    silvr_hash_t         hash;
} silvr_block_t;

/* Wallet */
typedef struct {
    uint8_t          privkey[32];
    silvr_pubkey_t   pubkey;
    silvr_address_t  address;
    silvr_amount_t   balance;
} silvr_wallet_t;

/* Function declarations */
void   silvr_sha256(const uint8_t *data, size_t len, uint8_t *out);
void   silvr_sha256d(const uint8_t *data, size_t len, uint8_t *out);
void   silvr_hash160(const uint8_t *data, size_t len, uint8_t *out);
int    silvr_wallet_create(silvr_wallet_t *wallet);
int    silvr_wallet_from_privkey(silvr_wallet_t *wallet, const uint8_t *privkey);
void   silvr_wallet_print(const silvr_wallet_t *wallet);
int    silvr_mine_block(silvr_block_t *block, uint32_t difficulty);
void   silvr_block_print(const silvr_block_t *block);
uint64_t silvr_block_reward(silvr_height_t height);
uint64_t silvr_treasury_cut(silvr_height_t height);
uint64_t silvr_miner_reward(silvr_height_t height);

#endif
EOF

# Create SHA256 and crypto implementation
echo "Creating crypto.c..."
cat > src/core/crypto.c << 'EOF'
#include "../../include/silvr.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>

void silvr_sha256(const uint8_t *data, size_t len, uint8_t *out) {
    SHA256(data, len, out);
}

void silvr_sha256d(const uint8_t *data, size_t len, uint8_t *out) {
    uint8_t tmp[32];
    SHA256(data, len, tmp);
    SHA256(tmp, 32, out);
}

void silvr_hash160(const uint8_t *data, size_t len, uint8_t *out) {
    uint8_t sha[32];
    SHA256(data, len, sha);
    RIPEMD160(sha, 32, out);
}
EOF

# Create wallet implementation
echo "Creating wallet.c..."
cat > src/wallet/wallet.c << 'EOF'
#include "../../include/silvr.h"
#include <secp256k1.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

static const char BASE58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZ"
    "abcdefghijkmnopqrstuvwxyz";

static void base58check_encode(const uint8_t *data,
                                size_t len, char *out) {
    uint8_t buf[128] = {0};
    memcpy(buf, data, len);
    uint8_t hash1[32], hash2[32];
    SHA256(buf, len, hash1);
    SHA256(hash1, 32, hash2);
    buf[len]   = hash2[0];
    buf[len+1] = hash2[1];
    buf[len+2] = hash2[2];
    buf[len+3] = hash2[3];
    size_t total = len + 4;

    /* count leading zeros */
    int zeros = 0;
    for (size_t i = 0; i < total && buf[i] == 0; i++)
        zeros++;

    /* base58 encode */
    uint8_t tmp[256] = {0};
    int idx = 0;
    /* simple bignum divide */
    uint8_t input[128];
    memcpy(input, buf, total);
    size_t input_len = total;

    while (input_len > 0) {
        uint32_t rem = 0;
        size_t new_len = 0;
        uint8_t new_input[128] = {0};
        for (size_t i = 0; i < input_len; i++) {
            uint32_t cur = rem * 256 + input[i];
            if (new_len > 0 || cur / 58 > 0) {
                new_input[new_len++] = cur / 58;
            }
            rem = cur % 58;
        }
        tmp[idx++] = BASE58[rem];
        memcpy(input, new_input, new_len);
        input_len = new_len;
    }

    /* add leading 1s */
    int out_idx = 0;
    for (int i = 0; i < zeros; i++)
        out[out_idx++] = '1';

    /* reverse */
    for (int i = idx - 1; i >= 0; i--)
        out[out_idx++] = tmp[i];
    out[out_idx] = '\0';
}

int silvr_wallet_create(silvr_wallet_t *wallet) {
    secp256k1_context *ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

    /* generate random private key */
    do {
        if (RAND_bytes(wallet->privkey, 32) != 1) {
            secp256k1_context_destroy(ctx);
            return -1;
        }
    } while (!secp256k1_ec_seckey_verify(ctx, wallet->privkey));

    /* derive public key */
    secp256k1_pubkey pubkey;
    secp256k1_ec_pubkey_create(ctx, &pubkey, wallet->privkey);

    size_t pubkey_len = 33;
    secp256k1_ec_pubkey_serialize(ctx, wallet->pubkey,
        &pubkey_len, &pubkey,
        SECP256K1_EC_COMPRESSED);

    /* derive address: version + HASH160(pubkey) */
    uint8_t hash160[20];
    uint8_t sha[32];
    SHA256(wallet->pubkey, 33, sha);
    RIPEMD160(sha, 32, hash160);

    uint8_t versioned[21];
    versioned[0] = SILVR_VERSION_BYTE;
    memcpy(versioned + 1, hash160, 20);

    base58check_encode(versioned, 21, wallet->address);

    secp256k1_context_destroy(ctx);
    return 0;
}

int silvr_wallet_from_privkey(silvr_wallet_t *wallet,
                               const uint8_t *privkey) {
    memcpy(wallet->privkey, privkey, 32);

    secp256k1_context *ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

    secp256k1_pubkey pubkey;
    secp256k1_ec_pubkey_create(ctx, &pubkey, wallet->privkey);

    size_t pubkey_len = 33;
    secp256k1_ec_pubkey_serialize(ctx, wallet->pubkey,
        &pubkey_len, &pubkey,
        SECP256K1_EC_COMPRESSED);

    uint8_t hash160[20];
    uint8_t sha[32];
    SHA256(wallet->pubkey, 33, sha);
    RIPEMD160(sha, 32, hash160);

    uint8_t versioned[21];
    versioned[0] = SILVR_VERSION_BYTE;
    memcpy(versioned + 1, hash160, 20);

    base58check_encode(versioned, 21, wallet->address);

    secp256k1_context_destroy(ctx);
    return 0;
}

void silvr_wallet_print(const silvr_wallet_t *wallet) {
    printf("\n=== SILVR WALLET ===\n");
    printf("Address : %s\n", wallet->address);
    printf("Privkey : ");
    for (int i = 0; i < 32; i++)
        printf("%02x", wallet->privkey[i]);
    printf("\n");
    printf("Pubkey  : ");
    for (int i = 0; i < 33; i++)
        printf("%02x", wallet->pubkey[i]);
    printf("\n");
    printf("====================\n\n");
}
EOF

# Create consensus implementation
echo "Creating consensus.c..."
cat > src/consensus/consensus.c << 'EOF'
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
EOF

# Create main node
echo "Creating main.c..."
cat > src/core/main.c << 'EOF'
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
        /* Generate new wallet */
        silvr_wallet_create(&miner_wallet);
        printf("Generated new wallet\n");
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
EOF

# Create Makefile
echo "Creating Makefile..."
cat > Makefile << 'EOF'
CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lsecp256k1 -lssl -lcrypto -lm

SRCS = src/core/main.c \
       src/core/crypto.c \
       src/wallet/wallet.c \
       src/consensus/consensus.c

TARGET = silvrd

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	@echo ""
	@echo "================================================"
	@echo "  SILVR node built successfully"
	@echo "  Run: ./silvrd"
	@echo "  Run with your key: ./silvrd YOUR_PRIVATE_KEY"
	@echo "================================================"

clean:
	rm -f $(TARGET)
EOF

# Build everything
echo ""
echo "Building SILVR production node..."
echo ""
make

echo ""
echo "================================================"
echo "  SETUP COMPLETE"
echo "================================================"
echo ""
echo "To start mining run:"
echo "  ./silvrd"
echo ""
echo "To mine with your existing wallet run:"
echo "  ./silvrd YOUR_PRIVATE_KEY_HERE"
echo ""
