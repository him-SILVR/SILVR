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
