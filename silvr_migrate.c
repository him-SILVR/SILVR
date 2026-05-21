/*
 * silvr_migrate.c — Migrate v2.4 miner address into v3.0
 * =======================================================
 * This tool:
 *   1. Takes your existing v2.4 miner address
 *   2. Decodes it to get the pubkey hash
 *   3. Saves it as miner_address.dat so silvrd_v3 uses it
 *   4. Creates a founding UTXO crediting your full v2.4 balance
 *      into the v3 chain as a genesis allocation
 *
 * Your private key is NEVER needed here.
 * Migration is purely address-based.
 */

#include "silvr_utxo.h"

/* Your v2.4 miner address */
#define V2_MINER_ADDR    "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE"

/* Your v2.4 mined balance in satoshis (6,664,081.97 SILVR) */
#define V2_BALANCE_SILVR 6664081.97
#define V2_BALANCE_SATS  ((uint64_t)(V2_BALANCE_SILVR * SILVR_SATOSHIS))

/* v2.4 blocks mined — for record keeping */
#define V2_BLOCKS_MINED  46913

#define MINER_ADDR_FILE  "miner_address.dat"

int main(void) {
    printf("========================================\n");
    printf(" SILVR v2.4 → v3.0 Address Migration   \n");
    printf("========================================\n\n");

    /* Step 1: Decode v2.4 address → pubkey hash */
    printf("[MIGRATE] Decoding address: %s\n", V2_MINER_ADDR);

    uint8_t pkhash[20];
    silvr_error_t e = crypto_addr_to_pkhash(V2_MINER_ADDR,
                                             SILVR_VERSION_BYTE,
                                             pkhash);
    if (e != SILVR_OK) {
        fprintf(stderr, "[MIGRATE] ERROR: Cannot decode address: %s\n",
                silvr_strerror(e));
        fprintf(stderr, "          Check the address is correct.\n");
        return 1;
    }

    printf("[MIGRATE] Address decoded OK\n");
    printf("[MIGRATE] PubkeyHash: ");
    for (int i = 0; i < 20; i++) printf("%02X", pkhash[i]);
    printf("\n\n");

    /* Step 2: Save pkhash + address string to miner_address.dat */
    /* Format: pkhash(20 bytes) + addr_str(40 bytes) */
    FILE *f = fopen(MINER_ADDR_FILE, "wb");
    if (!f) {
        fprintf(stderr, "[MIGRATE] Cannot create %s\n", MINER_ADDR_FILE);
        return 1;
    }
    fwrite(pkhash, 1, 20, f);
    char addr_str[40];
    memset(addr_str, 0, 40);
    strncpy(addr_str, V2_MINER_ADDR, 39);
    fwrite(addr_str, 1, 40, f);
    fclose(f);
    printf("[MIGRATE] Saved miner address to %s\n\n", MINER_ADDR_FILE);

    /* Step 3: Create founding UTXO in v3 UTXO set */
    printf("[MIGRATE] Loading v3 UTXO set...\n");
    utxo_load();

    /* Check if founding UTXO already exists */
    /* Use a special genesis migration txid: all 0xFF */
    uint8_t migration_txid[32];
    memset(migration_txid, 0xFF, 32);
    migration_txid[0] = 0x26; /* Chain ID 2026 marker */
    migration_txid[1] = 0x20;

    silvr_utxo_entry_t *existing = utxo_find(migration_txid, 0);
    if (existing) {
        printf("[MIGRATE] Founding UTXO already exists — skipping\n");
    } else {
        /* Add founding UTXO: full v2.4 balance credited to your address */
        utxo_error_t ue = utxo_add(migration_txid, 0,
                                    V2_BALANCE_SATS,
                                    pkhash,
                                    0); /* block height 0 = genesis */
        if (ue != UTXO_OK) {
            fprintf(stderr, "[MIGRATE] Failed to add founding UTXO: %s\n",
                    utxo_strerror(ue));
            return 1;
        }
        printf("[MIGRATE] Founding UTXO created: %.8f SILVR\n",
               (double)V2_BALANCE_SATS / SILVR_SATOSHIS);
    }

    /* Save UTXO set */
    utxo_error_t se = utxo_save();
    if (se != UTXO_OK) {
        fprintf(stderr, "[MIGRATE] Failed to save UTXO: %s\n",
                utxo_strerror(se));
        return 1;
    }

    printf("\n[MIGRATE] Verifying balance...\n");
    uint64_t balance = utxo_get_balance(pkhash);
    printf("[MIGRATE] Balance in v3: %.8f SILVR\n",
           (double)balance / SILVR_SATOSHIS);

    printf("\n========================================\n");
    printf(" Migration Summary                      \n");
    printf("========================================\n");
    printf("  Address   : %s\n", V2_MINER_ADDR);
    printf("  v2 blocks : %d\n", V2_BLOCKS_MINED);
    printf("  v2 balance: %.2f SILVR\n", V2_BALANCE_SILVR);
    printf("  v3 balance: %.8f SILVR\n",
           (double)balance / SILVR_SATOSHIS);
    printf("  Status    : %s\n",
           balance > 0 ? "SUCCESS" : "FAILED");
    printf("========================================\n");
    printf("\nNow run silvrd_v3.exe — it will mine\n");
    printf("new blocks to your original address.\n\n");

    return balance > 0 ? 0 : 1;
}
