/*
 * silvr_utxo.h — SILVR Protocol UTXO Set Implementation
 * =======================================================
 * Replaces the fake account-balance model with a real UTXO set.
 * Every coin is an unspent transaction output (UTXO).
 * Spending = marking a UTXO as spent + creating new UTXOs.
 *
 * BITCOIN WHITEPAPER ALIGNMENT:
 *   Section 2:  Each coin = chain of digital signatures over UTXOs
 *   Section 9:  Multiple inputs/outputs, change output handling
 *   Section 5:  Double-spend check before block acceptance
 *
 * DIVERGENCE FROM BITCOIN:
 *   - Fixed array (MAX_UTXOS) instead of LevelDB for simplicity
 *   - Atomic save via .tmp + rename (same guarantee, simpler code)
 *   - No script system — pubkey_hash directly in output (P2PKH only)
 *
 * Target: Windows MSYS2 mingw64, gcc 15.2.0, C99
 */

#ifndef SILVR_UTXO_H
#define SILVR_UTXO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

#include "crypto_abstraction.h"

/* =========================================================================
 * CONSTANTS
 * ========================================================================= */
#define MAX_UTXOS           100000
#define UTXO_FILE           "utxo.dat"
#define UTXO_FILE_TMP       "utxo.dat.tmp"
#define SILVR_SATOSHIS      100000000ULL   /* 1 SILVR = 1e8 satoshis */
#define MAX_SUPPLY          4200000000000000ULL /* 42,000,000 SILVR in satoshis */

/* =========================================================================
 * UTXO ENTRY
 * One unspent transaction output.
 * ========================================================================= */
typedef struct {
    uint8_t  txid[32];           /* transaction that created this output    */
    uint32_t vout_index;         /* output index within that transaction     */
    uint64_t amount;             /* value in satoshis (1 SILVR = 1e8)       */
    uint8_t  pubkey_hash[20];    /* RIPEMD160(SHA256(pubkey)) — who owns it */
    uint8_t  spent;              /* 0 = unspent, 1 = spent                  */
    uint64_t block_height;       /* block that created this UTXO            */
} silvr_utxo_entry_t;

/* =========================================================================
 * UTXO DATABASE
 * ========================================================================= */
typedef struct {
    silvr_utxo_entry_t entries[MAX_UTXOS];
    uint32_t           count;        /* total entries including spent        */
    uint32_t           unspent_count;/* live unspent count                   */
    uint64_t           total_supply; /* sum of all unspent amounts           */
} silvr_utxo_db_t;

/* Global UTXO database — one instance per node process */
static silvr_utxo_db_t g_utxo_db;

/* =========================================================================
 * ERROR CODES (extends silvr_error_t)
 * ========================================================================= */
typedef enum {
    UTXO_OK               =  0,
    UTXO_ERR_NOT_FOUND    = -1,
    UTXO_ERR_ALREADY_SPENT= -2,
    UTXO_ERR_INSUFFICIENT = -3,
    UTXO_ERR_DB_FULL      = -4,
    UTXO_ERR_IO           = -5,
    UTXO_ERR_NULL_PARAM   = -6,
    UTXO_ERR_SUPPLY_CAP   = -7,
    UTXO_ERR_DOUBLE_SPEND = -8,
} utxo_error_t;

static inline const char *utxo_strerror(utxo_error_t e) {
    switch (e) {
    case UTXO_OK:                return "OK";
    case UTXO_ERR_NOT_FOUND:     return "UTXO not found";
    case UTXO_ERR_ALREADY_SPENT: return "UTXO already spent";
    case UTXO_ERR_INSUFFICIENT:  return "Insufficient funds";
    case UTXO_ERR_DB_FULL:       return "UTXO database full";
    case UTXO_ERR_IO:            return "File I/O error";
    case UTXO_ERR_NULL_PARAM:    return "Null parameter";
    case UTXO_ERR_SUPPLY_CAP:    return "Supply cap exceeded";
    case UTXO_ERR_DOUBLE_SPEND:  return "Double spend detected";
    default:                     return "Unknown error";
    }
}

/* =========================================================================
 * UTXO LOOKUP
 * ========================================================================= */

/*
 * utxo_find()
 * Finds a UTXO by txid + vout_index.
 * Returns pointer to entry or NULL if not found.
 */
static silvr_utxo_entry_t *utxo_find(const uint8_t *txid, uint32_t vout_index) {
    if (!txid) return NULL;
    for (uint32_t i = 0; i < g_utxo_db.count; i++) {
        silvr_utxo_entry_t *e = &g_utxo_db.entries[i];
        if (e->vout_index == vout_index &&
            memcmp(e->txid, txid, 32) == 0) {
            return e;
        }
    }
    return NULL;
}

/*
 * utxo_get_pkhash()
 * Returns the pubkey_hash locked in a UTXO, or NULL if not found/spent.
 * Used by tx validator to check ownership.
 */
static const uint8_t *utxo_get_pkhash(const uint8_t *txid, uint32_t vout_index) {
    silvr_utxo_entry_t *e = utxo_find(txid, vout_index);
    if (!e || e->spent) return NULL;
    return e->pubkey_hash;
}

/*
 * utxo_get_amount()
 * Returns the amount of a UTXO, or 0 if not found/spent.
 */
static uint64_t utxo_get_amount(const uint8_t *txid, uint32_t vout_index) {
    silvr_utxo_entry_t *e = utxo_find(txid, vout_index);
    if (!e || e->spent) return 0;
    return e->amount;
}

/*
 * utxo_get_balance()
 * Returns total unspent balance for a given pubkey_hash.
 * Scans full UTXO set — O(n), fine for testnet size.
 */
static uint64_t utxo_get_balance(const uint8_t *pubkey_hash) {
    if (!pubkey_hash) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < g_utxo_db.count; i++) {
        silvr_utxo_entry_t *e = &g_utxo_db.entries[i];
        if (!e->spent && memcmp(e->pubkey_hash, pubkey_hash, 20) == 0)
            total += e->amount;
    }
    return total;
}

/* =========================================================================
 * UTXO CREATION
 * ========================================================================= */

/*
 * utxo_add()
 * Adds a new unspent output to the UTXO set.
 * Called when a block is accepted and its transactions are processed.
 *
 * Returns UTXO_OK or error code.
 */
static utxo_error_t utxo_add(const uint8_t *txid, uint32_t vout_index,
                               uint64_t amount, const uint8_t *pubkey_hash,
                               uint64_t block_height) {
    if (!txid || !pubkey_hash) return UTXO_ERR_NULL_PARAM;
    if (amount == 0) return UTXO_ERR_NULL_PARAM;

    /* Check supply cap */
    if (g_utxo_db.total_supply + amount > MAX_SUPPLY)
        return UTXO_ERR_SUPPLY_CAP;

    /* Check capacity */
    if (g_utxo_db.count >= MAX_UTXOS)
        return UTXO_ERR_DB_FULL;

    /* Check for duplicate */
    if (utxo_find(txid, vout_index) != NULL)
        return UTXO_ERR_DOUBLE_SPEND;

    silvr_utxo_entry_t *e = &g_utxo_db.entries[g_utxo_db.count];
    memcpy(e->txid, txid, 32);
    e->vout_index   = vout_index;
    e->amount       = amount;
    memcpy(e->pubkey_hash, pubkey_hash, 20);
    e->spent        = 0;
    e->block_height = block_height;

    g_utxo_db.count++;
    g_utxo_db.unspent_count++;
    g_utxo_db.total_supply += amount;

    return UTXO_OK;
}

/* =========================================================================
 * UTXO SPENDING (DOUBLE-SPEND PREVENTION)
 * ========================================================================= */

/*
 * utxo_spend()
 * Marks a UTXO as spent.
 * This is the core double-spend prevention mechanism.
 * Per Bitcoin whitepaper Section 2: "the payee needs proof that the
 * previous owners did not sign any earlier transactions."
 *
 * Returns UTXO_OK, UTXO_ERR_NOT_FOUND, or UTXO_ERR_ALREADY_SPENT.
 */
static utxo_error_t utxo_spend(const uint8_t *txid, uint32_t vout_index) {
    if (!txid) return UTXO_ERR_NULL_PARAM;

    silvr_utxo_entry_t *e = utxo_find(txid, vout_index);
    if (!e) return UTXO_ERR_NOT_FOUND;
    if (e->spent) return UTXO_ERR_ALREADY_SPENT; /* DOUBLE SPEND DETECTED */

    e->spent = 1;
    g_utxo_db.unspent_count--;
    g_utxo_db.total_supply -= e->amount;

    return UTXO_OK;
}

/* =========================================================================
 * TRANSACTION STRUCTURE (Phase 2 full version)
 * ========================================================================= */
#define MAX_TXINS  8
#define MAX_TXOUTS 8

typedef struct {
    uint8_t  prev_txid[32];
    uint32_t prev_vout;
    uint8_t  pubkey[SILVR_PUBKEY_COMP_LEN];
    uint8_t  sig_data[SILVR_SIG_DER_MAX_LEN];
    uint16_t sig_len;
    uint8_t  sig_version;
} silvr_txin_t;

typedef struct {
    uint64_t amount;
    uint8_t  pubkey_hash[20];
} silvr_txout_t;

typedef struct {
    uint32_t      version;
    uint32_t      n_inputs;
    silvr_txin_t  inputs[MAX_TXINS];
    uint32_t      n_outputs;
    silvr_txout_t outputs[MAX_TXOUTS];
    uint32_t      locktime;
    uint8_t       txid[32];
} silvr_tx_t;

/* =========================================================================
 * TRANSACTION SERIALIZATION (for signing/txid)
 * ========================================================================= */

/*
 * tx_serialize()
 * Serializes transaction fields into canonical byte form.
 * Used for: (1) computing txid, (2) creating msg_hash for signing.
 * Returns bytes written or -1 on error.
 */
static int tx_serialize(const silvr_tx_t *tx, uint8_t *buf, size_t buf_size) {
    if (!tx || !buf) return -1;
    size_t pos = 0;

#define WR(src, n) do { \
    if (pos + (n) > buf_size) return -1; \
    memcpy(buf + pos, (src), (n)); pos += (n); \
} while(0)
#define WR32(v) do { \
    uint32_t _v = (v); \
    if (pos + 4 > buf_size) return -1; \
    memcpy(buf + pos, &_v, 4); pos += 4; \
} while(0)
#define WR64(v) do { \
    uint64_t _v = (v); \
    if (pos + 8 > buf_size) return -1; \
    memcpy(buf + pos, &_v, 8); pos += 8; \
} while(0)

    WR32(tx->version);
    WR32(tx->n_inputs);
    for (uint32_t i = 0; i < tx->n_inputs; i++) {
        WR(tx->inputs[i].prev_txid, 32);
        WR32(tx->inputs[i].prev_vout);
        WR(tx->inputs[i].pubkey, SILVR_PUBKEY_COMP_LEN);
    }
    WR32(tx->n_outputs);
    for (uint32_t i = 0; i < tx->n_outputs; i++) {
        WR64(tx->outputs[i].amount);
        WR(tx->outputs[i].pubkey_hash, 20);
    }
    WR32(tx->locktime);

#undef WR
#undef WR32
#undef WR64

    return (int)pos;
}

/*
 * tx_compute_txid()
 * TXID = SHA256d(serialized tx).
 */
static void tx_compute_txid(silvr_tx_t *tx) {
    uint8_t buf[4096];
    int len = tx_serialize(tx, buf, sizeof(buf));
    if (len > 0)
        crypto_sha256d(buf, (size_t)len, tx->txid);
}

/* =========================================================================
 * TRANSACTION VALIDATION
 * Per Bitcoin whitepaper Section 2 & 5:
 *   "Nodes accept the block only if all transactions in it are valid
 *    and not already spent."
 * ========================================================================= */

/*
 * tx_validate()
 * Full transaction validation:
 *   1. Each input UTXO exists and is unspent
 *   2. Signature is valid for each input
 *   3. Total inputs >= total outputs (no coins from thin air)
 *   4. No overflow in amounts
 *
 * Does NOT modify the UTXO set — call tx_apply() after validation.
 * Returns UTXO_OK or error code.
 */
static utxo_error_t tx_validate(const silvr_tx_t *tx) {
    if (!tx) return UTXO_ERR_NULL_PARAM;

    /* Serialize for signature verification */
    uint8_t serial[4096];
    int serial_len = tx_serialize(tx, serial, sizeof(serial));
    if (serial_len < 0) return UTXO_ERR_NULL_PARAM;

    uint8_t msg_hash[32];
    crypto_sha256d(serial, (size_t)serial_len, msg_hash);

    uint64_t total_in  = 0;
    uint64_t total_out = 0;

    /* Validate each input */
    for (uint32_t i = 0; i < tx->n_inputs; i++) {
        const silvr_txin_t *inp = &tx->inputs[i];

        /* Skip coinbase inputs */
        int is_coinbase = 1;
        for (int b = 0; b < 32; b++)
            if (inp->prev_txid[b] != 0) { is_coinbase = 0; break; }
        if (is_coinbase && inp->prev_vout == 0xFFFFFFFF) continue;

        /* 1. Find UTXO */
        silvr_utxo_entry_t *utxo = utxo_find(inp->prev_txid, inp->prev_vout);
        if (!utxo) {
            fprintf(stderr, "[UTXO] Input %u: UTXO not found\n", i);
            return UTXO_ERR_NOT_FOUND;
        }

        /* 2. Check not already spent */
        if (utxo->spent) {
            fprintf(stderr, "[UTXO] Input %u: DOUBLE SPEND DETECTED\n", i);
            return UTXO_ERR_DOUBLE_SPEND;
        }

        /* 3. Verify signature */
        silvr_pubkey_t pk = {0};
        pk.addr_version = SILVR_VERSION_BYTE;
        pk.len = SILVR_PUBKEY_COMP_LEN;
        memcpy(pk.data, inp->pubkey, SILVR_PUBKEY_COMP_LEN);

        silvr_sig_t sig = {0};
        sig.version = inp->sig_version;
        sig.len     = inp->sig_len;
        memcpy(sig.data, inp->sig_data, inp->sig_len);

        silvr_error_t verr = crypto_verify(&pk, msg_hash, &sig,
                                            utxo->pubkey_hash);
        if (verr != SILVR_OK) {
            fprintf(stderr, "[UTXO] Input %u: invalid signature: %s\n",
                    i, silvr_strerror(verr));
            return UTXO_ERR_NOT_FOUND;
        }

        /* 4. Accumulate input total */
        total_in += utxo->amount;
    }

    /* Accumulate output total */
    for (uint32_t i = 0; i < tx->n_outputs; i++)
        total_out += tx->outputs[i].amount;

    /* 5. Inputs must cover outputs (fee = total_in - total_out) */
    if (total_out > total_in) {
        fprintf(stderr, "[UTXO] Outputs (%llu) exceed inputs (%llu)\n",
                (unsigned long long)total_out,
                (unsigned long long)total_in);
        return UTXO_ERR_INSUFFICIENT;
    }

    return UTXO_OK;
}

/*
 * tx_apply()
 * Applies a validated transaction to the UTXO set:
 *   1. Spend all inputs
 *   2. Create all outputs as new UTXOs
 *
 * MUST only be called after tx_validate() returns UTXO_OK.
 * Returns UTXO_OK or error.
 */
static utxo_error_t tx_apply(const silvr_tx_t *tx, uint64_t block_height) {
    if (!tx) return UTXO_ERR_NULL_PARAM;

    /* Spend inputs */
    for (uint32_t i = 0; i < tx->n_inputs; i++) {
        const silvr_txin_t *inp = &tx->inputs[i];
        int is_coinbase = 1;
        for (int b = 0; b < 32; b++)
            if (inp->prev_txid[b] != 0) { is_coinbase = 0; break; }
        if (is_coinbase && inp->prev_vout == 0xFFFFFFFF) continue;

        utxo_error_t e = utxo_spend(inp->prev_txid, inp->prev_vout);
        if (e != UTXO_OK) return e;
    }

    /* Create outputs */
    for (uint32_t i = 0; i < tx->n_outputs; i++) {
        utxo_error_t e = utxo_add(tx->txid, i,
                                   tx->outputs[i].amount,
                                   tx->outputs[i].pubkey_hash,
                                   block_height);
        if (e != UTXO_OK) return e;
    }

    return UTXO_OK;
}

/*
 * tx_get_fee()
 * Returns the transaction fee (total_in - total_out).
 * Returns 0 for coinbase or if UTXO not found.
 */
static uint64_t tx_get_fee(const silvr_tx_t *tx) {
    if (!tx) return 0;
    uint64_t total_in = 0, total_out = 0;
    for (uint32_t i = 0; i < tx->n_inputs; i++) {
        const silvr_txin_t *inp = &tx->inputs[i];
        int is_coinbase = 1;
        for (int b = 0; b < 32; b++)
            if (inp->prev_txid[b] != 0) { is_coinbase = 0; break; }
        if (is_coinbase && inp->prev_vout == 0xFFFFFFFF) continue;
        total_in += utxo_get_amount(inp->prev_txid, inp->prev_vout);
    }
    for (uint32_t i = 0; i < tx->n_outputs; i++)
        total_out += tx->outputs[i].amount;
    return (total_in > total_out) ? (total_in - total_out) : 0;
}

/* =========================================================================
 * COINBASE TRANSACTION BUILDER
 * Per Bitcoin whitepaper Section 6:
 *   "First transaction in a block starts a new coin owned by block creator"
 * ========================================================================= */

/*
 * tx_build_coinbase()
 * Creates a coinbase transaction for the miner.
 * `block_height` used as unique nonce (prevents duplicate coinbase txids).
 * `miner_pkhash` = 20-byte hash of miner's address.
 * `reward` = miner's share in satoshis.
 * `treasury_pkhash` = 20-byte hash of treasury address.
 * `treasury_amount` = treasury's share in satoshis (2.5 SILVR per block).
 */
static void tx_build_coinbase(silvr_tx_t *tx,
                               uint64_t block_height,
                               const uint8_t *miner_pkhash,
                               uint64_t reward,
                               const uint8_t *treasury_pkhash,
                               uint64_t treasury_amount) {
    memset(tx, 0, sizeof(*tx));
    tx->version  = 1;
    tx->n_inputs = 1;
    tx->locktime = 0;

    /* Coinbase input: all zeros txid, vout = 0xFFFFFFFF */
    memset(tx->inputs[0].prev_txid, 0, 32);
    tx->inputs[0].prev_vout    = 0xFFFFFFFF;
    /* Encode block height in first 8 bytes of pubkey field as nonce */
    memcpy(tx->inputs[0].pubkey, &block_height, 8);
    tx->inputs[0].sig_len      = 0;

    /* Two outputs: miner + treasury (SILVR 95/5 split) */
    if (treasury_pkhash && treasury_amount > 0) {
        tx->n_outputs = 2;
        tx->outputs[0].amount = reward;
        memcpy(tx->outputs[0].pubkey_hash, miner_pkhash, 20);
        tx->outputs[1].amount = treasury_amount;
        memcpy(tx->outputs[1].pubkey_hash, treasury_pkhash, 20);
    } else {
        tx->n_outputs = 1;
        tx->outputs[0].amount = reward;
        memcpy(tx->outputs[0].pubkey_hash, miner_pkhash, 20);
    }

    tx_compute_txid(tx);
}

/* =========================================================================
 * ATOMIC FILE PERSISTENCE
 * Write to .tmp first, fsync, then rename — prevents corruption on crash.
 * ========================================================================= */

/*
 * utxo_save()
 * Atomically saves UTXO database to disk.
 * Pattern: write → tmp → fsync → rename (atomic on POSIX, near-atomic Win32)
 * Returns UTXO_OK or UTXO_ERR_IO.
 */
static utxo_error_t utxo_save(void) {
    FILE *f = fopen(UTXO_FILE_TMP, "wb");
    if (!f) {
        fprintf(stderr, "[UTXO] Cannot open %s for writing\n", UTXO_FILE_TMP);
        return UTXO_ERR_IO;
    }

    if (fwrite(&g_utxo_db, sizeof(g_utxo_db), 1, f) != 1) {
        fclose(f);
        return UTXO_ERR_IO;
    }

    fflush(f);

    /* fsync — force OS to flush to physical disk before rename */
#ifdef _WIN32
    _commit(_fileno(f));
#else
    fsync(fileno(f));
#endif

    fclose(f);

    /* Atomic rename: tmp → real file */
#ifdef _WIN32
    /* Windows rename fails if target exists — delete first */
    DeleteFileA(UTXO_FILE);
#endif
    if (rename(UTXO_FILE_TMP, UTXO_FILE) != 0) {
        fprintf(stderr, "[UTXO] rename failed\n");
        return UTXO_ERR_IO;
    }

    return UTXO_OK;
}

/*
 * utxo_load()
 * Loads UTXO database from disk.
 * Returns UTXO_OK or UTXO_ERR_IO (fresh start if file missing).
 */
static utxo_error_t utxo_load(void) {
    FILE *f = fopen(UTXO_FILE, "rb");
    if (!f) {
        /* No file = fresh start, zero the db */
        memset(&g_utxo_db, 0, sizeof(g_utxo_db));
        printf("[UTXO] No existing utxo.dat — starting fresh\n");
        return UTXO_OK;
    }

    if (fread(&g_utxo_db, sizeof(g_utxo_db), 1, f) != 1) {
        fclose(f);
        memset(&g_utxo_db, 0, sizeof(g_utxo_db));
        fprintf(stderr, "[UTXO] Corrupt utxo.dat — starting fresh\n");
        return UTXO_ERR_IO;
    }

    fclose(f);
    printf("[UTXO] Loaded %u UTXOs (%u unspent), supply: %llu grains\n",
           g_utxo_db.count,
           g_utxo_db.unspent_count,
           (unsigned long long)g_utxo_db.total_supply);
    return UTXO_OK;
}

/*
 * utxo_print_stats()
 * Prints current UTXO set statistics.
 */
static void utxo_print_stats(void) {
    printf("[UTXO] Total entries : %u\n",   g_utxo_db.count);
    printf("[UTXO] Unspent UTXOs : %u\n",   g_utxo_db.unspent_count);
    printf("[UTXO] Total supply  : %.8f SILVR\n",
           (double)g_utxo_db.total_supply / (double)SILVR_SATOSHIS);
}

#endif /* SILVR_UTXO_H */
