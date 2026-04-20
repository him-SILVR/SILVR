/* SILVR UTXO Module
   Tracks balances for all addresses on the SILVR chain.
   Part of silvrd_v2.1 - The People's Chain
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define UTXO_FILE     "utxo.dat"
#define MAX_ADDRESSES 10000
#define ADDR_LEN      64

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
