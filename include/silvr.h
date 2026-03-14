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
