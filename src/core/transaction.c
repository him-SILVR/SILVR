/*
 * SILVR Transaction Signing
 * Chain ID: 2026 — The People's Chain
 *
 * Handles creating, signing, and verifying
 * SILVR transactions using secp256k1
 */

#include "../../include/silvr.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <secp256k1.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Create a new transaction ─────────────────── */
silvr_tx_t* silvr_tx_create(
    const silvr_address_t from,
    const silvr_address_t to,
    silvr_amount_t        amount,
    silvr_amount_t        fee)
{
    silvr_tx_t* tx = calloc(1, sizeof(silvr_tx_t));
    if (!tx) return NULL;

    tx->version     = SILVR_VERSION_BYTE;
    tx->num_inputs  = 1;
    tx->num_outputs = 2;  /* to + change */
    tx->locktime    = 0;

    /* Generate transaction ID */
    uint8_t txdata[256];
    size_t  txlen = 0;

    memcpy(txdata + txlen, from, sizeof(silvr_address_t));
    txlen += sizeof(silvr_address_t);
    memcpy(txdata + txlen, to, sizeof(silvr_address_t));
    txlen += sizeof(silvr_address_t);
    memcpy(txdata + txlen, &amount, sizeof(silvr_amount_t));
    txlen += sizeof(silvr_amount_t);
    memcpy(txdata + txlen, &fee, sizeof(silvr_amount_t));
    txlen += sizeof(silvr_amount_t);

    /* Double SHA256 for txid */
    uint8_t hash1[32], hash2[32];
    SHA256(txdata, txlen, hash1);
    SHA256(hash1,  32,    hash2);
    memcpy(&tx->txid, hash2, sizeof(silvr_hash_t));

    return tx;
}

/* ── Sign a transaction ───────────────────────── */
int silvr_tx_sign(
    silvr_tx_t*      tx,
    const uint8_t*   privkey,
    silvr_sig_t*     sig_out)
{
    if (!tx || !privkey || !sig_out) return -1;

    secp256k1_context* ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN
    );
    if (!ctx) return -1;

    /* Hash the transaction for signing */
    uint8_t tx_hash[32];
    uint8_t tmp[32];
    SHA256((uint8_t*)&tx->txid,
           sizeof(silvr_hash_t), tmp);
    SHA256(tmp, 32, tx_hash);

    /* Sign with secp256k1 */
    secp256k1_ecdsa_signature sig;
    int result = secp256k1_ecdsa_sign(
        ctx,
        &sig,
        tx_hash,
        privkey,
        NULL,
        NULL
    );

    if (result) {
        /* Serialize signature */
        size_t siglen = 64;
        secp256k1_ecdsa_signature_serialize_compact(
            ctx,
            (uint8_t*)sig_out,
            &sig
        );
    }

    secp256k1_context_destroy(ctx);
    return result ? 0 : -1;
}

/* ── Verify a transaction signature ──────────── */
int silvr_tx_verify(
    const silvr_tx_t*   tx,
    const silvr_sig_t*  sig,
    const uint8_t*      pubkey,
    size_t              pubkey_len)
{
    if (!tx || !sig || !pubkey) return -1;

    secp256k1_context* ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_VERIFY
    );
    if (!ctx) return -1;

    /* Hash the transaction */
    uint8_t tx_hash[32];
    uint8_t tmp[32];
    SHA256((uint8_t*)&tx->txid,
           sizeof(silvr_hash_t), tmp);
    SHA256(tmp, 32, tx_hash);

    /* Parse public key */
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_parse(
            ctx, &pk, pubkey, pubkey_len)) {
        secp256k1_context_destroy(ctx);
        return -1;
    }

    /* Parse signature */
    secp256k1_ecdsa_signature s;
    if (!secp256k1_ecdsa_signature_parse_compact(
            ctx, &s, (const uint8_t*)sig)) {
        secp256k1_context_destroy(ctx);
        return -1;
    }

    /* Verify */
    int valid = secp256k1_ecdsa_verify(
        ctx, &s, tx_hash, &pk
    );

    secp256k1_context_destroy(ctx);
    return valid ? 0 : -1;
}

/* ── Print transaction details ───────────────── */
void silvr_tx_print(const silvr_tx_t* tx) {
    if (!tx) return;

    printf("=== SILVR TRANSACTION ===\n");
    printf("Version  : %u\n", tx->version);
    printf("Inputs   : %u\n", tx->num_inputs);
    printf("Outputs  : %u\n", tx->num_outputs);
    printf("Locktime : %u\n", tx->locktime);
    printf("TXID     : ");
    for (int i = 0; i < 32; i++) {
        printf("%02x",
            ((uint8_t*)&tx->txid)[i]);
    }
    printf("\n");
    printf("=========================\n");
}

/* ── Free transaction ────────────────────────── */
void silvr_tx_free(silvr_tx_t* tx) {
    if (tx) free(tx);
}
