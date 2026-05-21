#ifndef SILVR_CRYPTO_ABSTRACTION_H
#define SILVR_CRYPTO_ABSTRACTION_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

#include <secp256k1.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/rand.h>

/* =========================================================================
 * ERROR CODES
 * ========================================================================= */
typedef enum {
    SILVR_OK                   =  0,
    SILVR_ERR_NULL_PARAM       = -1,
    SILVR_ERR_INVALID_PRIVKEY  = -2,
    SILVR_ERR_INVALID_PUBKEY   = -3,
    SILVR_ERR_INVALID_SIG      = -4,
    SILVR_ERR_INVALID_ADDR     = -5,
    SILVR_ERR_BAD_CHECKSUM     = -6,
    SILVR_ERR_BAD_VERSION      = -7,
    SILVR_ERR_BUFFER_TOO_SMALL = -8,
    SILVR_ERR_RAND_FAIL        = -9,
    SILVR_ERR_SIGN_FAIL        = -10,
    SILVR_ERR_VERIFY_FAIL      = -11,
    SILVR_ERR_UNKNOWN_SIGVER   = -12,
    SILVR_ERR_SERIALIZE        = -13,
} silvr_error_t;

static inline const char *silvr_strerror(silvr_error_t e) {
    switch (e) {
    case SILVR_OK:                   return "OK";
    case SILVR_ERR_NULL_PARAM:       return "Null parameter";
    case SILVR_ERR_INVALID_PRIVKEY:  return "Invalid private key";
    case SILVR_ERR_INVALID_PUBKEY:   return "Invalid public key";
    case SILVR_ERR_INVALID_SIG:      return "Invalid signature";
    case SILVR_ERR_INVALID_ADDR:     return "Invalid address";
    case SILVR_ERR_BAD_CHECKSUM:     return "Bad checksum";
    case SILVR_ERR_BAD_VERSION:      return "Bad version byte";
    case SILVR_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
    case SILVR_ERR_RAND_FAIL:        return "RNG failure";
    case SILVR_ERR_SIGN_FAIL:        return "Signing failure";
    case SILVR_ERR_VERIFY_FAIL:      return "Verification failure";
    case SILVR_ERR_UNKNOWN_SIGVER:   return "Unknown sig_version";
    case SILVR_ERR_SERIALIZE:        return "Serialization error";
    default:                         return "Unknown error";
    }
}

/* =========================================================================
 * VERSIONING
 * ========================================================================= */
typedef enum {
    SILVR_SIGVER_ECDSA_COMPACT = 0,
    SILVR_SIGVER_ECDSA_DER     = 1,
    SILVR_SIGVER_DILITHIUM2    = 2,
    SILVR_SIGVER_FALCON512     = 3,
} silvr_sigver_t;

typedef enum {
    SILVR_ADDRVER_P2PKH   = 0x3F,
    SILVR_ADDRVER_PQ_HASH = 0x40,
} silvr_addrver_t;

#define SILVR_PQ_ACTIVATION_HEIGHT  1000000ULL
#define SILVR_VERSION_BYTE          ((uint8_t)SILVR_ADDRVER_P2PKH)
#define SILVR_PRIVKEY_LEN           32
#define SILVR_PUBKEY_COMP_LEN       33
#define SILVR_PUBKEY_UNCOMP_LEN     65
#define SILVR_PKHASH_LEN            20
#define SILVR_ADDR_STR_MAX          40
#define SILVR_SIG_COMPACT_LEN       64
#define SILVR_SIG_DER_MAX_LEN       72
#define SILVR_TXID_LEN              32

/* =========================================================================
 * CORE TYPES
 * ========================================================================= */
typedef struct {
    uint8_t  version;
    uint8_t  data[SILVR_SIG_DER_MAX_LEN];
    uint16_t len;
} silvr_sig_t;

typedef struct {
    uint8_t  addr_version;
    uint8_t  data[SILVR_PUBKEY_UNCOMP_LEN];
    uint16_t len;
} silvr_pubkey_t;

typedef struct {
    uint8_t        privkey[SILVR_PRIVKEY_LEN];
    silvr_pubkey_t pubkey;
    uint8_t        pkhash[SILVR_PKHASH_LEN];
    char           addr_str[SILVR_ADDR_STR_MAX];
    uint8_t        sig_version;
} silvr_keypair_t;

/* =========================================================================
 * HASH PRIMITIVES
 * ========================================================================= */
static inline void crypto_sha256(const uint8_t *in, size_t len, uint8_t *out) {
    SHA256(in, len, out);
}

static inline void crypto_sha256d(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t tmp[32];
    SHA256(in, len, tmp);
    SHA256(tmp, 32, out);
}

static inline void crypto_hash160(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t sha[32];
    SHA256(in, len, sha);
    RIPEMD160(sha, 32, out);
}

/* =========================================================================
 * BASE58CHECK
 * ========================================================================= */
static const char SILVR_B58_ALPHA[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static silvr_error_t crypto_base58_encode(const uint8_t *in, size_t in_len,
                                           char *out, size_t out_len) {
    if (!in || !out) return SILVR_ERR_NULL_PARAM;
    size_t leading = 0;
    while (leading < in_len && in[leading] == 0) leading++;
    size_t buf_sz = in_len * 138 / 100 + 2;
    uint8_t *buf = (uint8_t *)calloc(buf_sz, 1);
    if (!buf) return SILVR_ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < in_len; i++) {
        int carry = in[i];
        for (int j = (int)buf_sz - 1; j >= 0; j--) {
            carry += 256 * buf[j];
            buf[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
    }
    size_t start = 0;
    while (start < buf_sz && buf[start] == 0) start++;
    size_t needed = leading + (buf_sz - start) + 1;
    if (needed > out_len) { free(buf); return SILVR_ERR_BUFFER_TOO_SMALL; }
    size_t idx = 0;
    for (size_t i = 0; i < leading; i++)    out[idx++] = '1';
    for (size_t i = start; i < buf_sz; i++) out[idx++] = SILVR_B58_ALPHA[buf[i]];
    out[idx] = '\0';
    free(buf);
    return SILVR_OK;
}

static int crypto_base58_decode(const char *str,
                                 uint8_t *out, size_t out_len) {
    if (!str || !out) return SILVR_ERR_NULL_PARAM;
    size_t slen = strlen(str);
    size_t leading = 0;
    while (leading < slen && str[leading] == '1') leading++;
    size_t buf_sz = slen * 733 / 1000 + 2;
    uint8_t *buf = (uint8_t *)calloc(buf_sz, 1);
    if (!buf) return SILVR_ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < slen; i++) {
        const char *p = strchr(SILVR_B58_ALPHA, str[i]);
        if (!p) { free(buf); return SILVR_ERR_INVALID_ADDR; }
        int carry = (int)(p - SILVR_B58_ALPHA);
        for (int j = (int)buf_sz - 1; j >= 0; j--) {
            carry += 58 * buf[j];
            buf[j] = (uint8_t)(carry % 256);
            carry /= 256;
        }
    }
    size_t start = 0;
    while (start < buf_sz && buf[start] == 0) start++;
    size_t total = leading + (buf_sz - start);
    if (total > out_len) { free(buf); return SILVR_ERR_BUFFER_TOO_SMALL; }
    memset(out, 0, leading);
    memcpy(out + leading, buf + start, buf_sz - start);
    free(buf);
    return (int)total;
}

/* =========================================================================
 * ADDRESS DERIVATION
 * ========================================================================= */
static silvr_error_t crypto_pubkey_to_addr(const uint8_t *pubkey,
                                            size_t pubkey_len,
                                            uint8_t addr_version,
                                            uint8_t *pkhash_out,
                                            char *addr_out,
                                            size_t addr_out_len) {
    if (!pubkey || !addr_out) return SILVR_ERR_NULL_PARAM;
    uint8_t pkhash[20];
    crypto_hash160(pubkey, pubkey_len, pkhash);
    if (pkhash_out) memcpy(pkhash_out, pkhash, 20);
    uint8_t versioned[21];
    versioned[0] = addr_version;
    memcpy(versioned + 1, pkhash, 20);
    uint8_t cs[32];
    crypto_sha256d(versioned, 21, cs);
    uint8_t payload[25];
    memcpy(payload,      versioned, 21);
    memcpy(payload + 21, cs,         4);
    return crypto_base58_encode(payload, 25, addr_out, addr_out_len);
}

static silvr_error_t crypto_addr_to_pkhash(const char *addr,
                                            uint8_t expected_version,
                                            uint8_t *pkhash_out) {
    if (!addr || !pkhash_out) return SILVR_ERR_NULL_PARAM;
    uint8_t payload[30];
    int n = crypto_base58_decode(addr, payload, sizeof(payload));
    if (n != 25) return SILVR_ERR_INVALID_ADDR;
    if (payload[0] != expected_version) return SILVR_ERR_BAD_VERSION;
    uint8_t cs[32];
    crypto_sha256d(payload, 21, cs);
    if (memcmp(cs, payload + 21, 4) != 0) return SILVR_ERR_BAD_CHECKSUM;
    memcpy(pkhash_out, payload + 1, 20);
    return SILVR_OK;
}

/* =========================================================================
 * KEY GENERATION
 * ========================================================================= */
static silvr_error_t crypto_keygen(silvr_keypair_t *kp) {
    if (!kp) return SILVR_ERR_NULL_PARAM;
    memset(kp, 0, sizeof(*kp));
    secp256k1_context *ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN |
                                  SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return SILVR_ERR_RAND_FAIL;
    uint8_t rnd[32];
    if (RAND_bytes(rnd, 32) != 1) {
        secp256k1_context_destroy(ctx);
        return SILVR_ERR_RAND_FAIL;
    }
    secp256k1_context_randomize(ctx, rnd);
    secp256k1_pubkey raw_pub;
    uint8_t attempts = 0;
    do {
        if (RAND_bytes(kp->privkey, 32) != 1 || ++attempts > 10) {
            secp256k1_context_destroy(ctx);
            return SILVR_ERR_RAND_FAIL;
        }
    } while (!secp256k1_ec_pubkey_create(ctx, &raw_pub, kp->privkey));
    kp->pubkey.addr_version = SILVR_VERSION_BYTE;
    kp->pubkey.len          = SILVR_PUBKEY_COMP_LEN;
    size_t pub_len = SILVR_PUBKEY_COMP_LEN;
    secp256k1_ec_pubkey_serialize(ctx, kp->pubkey.data, &pub_len,
                                   &raw_pub, SECP256K1_EC_COMPRESSED);
    kp->sig_version = SILVR_SIGVER_ECDSA_DER;
    silvr_error_t e = crypto_pubkey_to_addr(
        kp->pubkey.data, kp->pubkey.len,
        SILVR_VERSION_BYTE,
        kp->pkhash, kp->addr_str, sizeof(kp->addr_str));
    secp256k1_context_destroy(ctx);
    return e;
}

/* =========================================================================
 * SIGNING
 * ========================================================================= */
static silvr_error_t crypto_sign(const uint8_t *privkey,
                                  const uint8_t *msg_hash,
                                  uint8_t        sig_version,
                                  silvr_sig_t   *sig_out) {
    if (!privkey || !msg_hash || !sig_out) return SILVR_ERR_NULL_PARAM;
    memset(sig_out, 0, sizeof(*sig_out));
    sig_out->version = sig_version;
    switch ((silvr_sigver_t)sig_version) {
    case SILVR_SIGVER_ECDSA_COMPACT:
    case SILVR_SIGVER_ECDSA_DER: {
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
        if (!ctx) return SILVR_ERR_RAND_FAIL;
        uint8_t rnd[32];
        if (RAND_bytes(rnd, 32) == 1)
            secp256k1_context_randomize(ctx, rnd);
        secp256k1_ecdsa_signature raw;
        if (!secp256k1_ecdsa_sign(ctx, &raw, msg_hash, privkey, NULL, NULL)) {
            secp256k1_context_destroy(ctx);
            return SILVR_ERR_SIGN_FAIL;
        }
        secp256k1_ecdsa_signature_normalize(ctx, &raw, &raw);
        if (sig_version == SILVR_SIGVER_ECDSA_COMPACT) {
            secp256k1_ecdsa_signature_serialize_compact(ctx, sig_out->data, &raw);
            sig_out->len = SILVR_SIG_COMPACT_LEN;
        } else {
            size_t der_len = SILVR_SIG_DER_MAX_LEN;
            secp256k1_ecdsa_signature_serialize_der(ctx, sig_out->data,
                                                     &der_len, &raw);
            sig_out->len = (uint16_t)der_len;
        }
        secp256k1_context_destroy(ctx);
        return SILVR_OK;
    }
    case SILVR_SIGVER_DILITHIUM2:
    case SILVR_SIGVER_FALCON512:
        fprintf(stderr, "[SILVR] PQ signing not yet implemented\n");
        return SILVR_ERR_UNKNOWN_SIGVER;
    default:
        return SILVR_ERR_UNKNOWN_SIGVER;
    }
}

/* =========================================================================
 * VERIFICATION
 * ========================================================================= */
static silvr_error_t crypto_verify(const silvr_pubkey_t *pubkey,
                                    const uint8_t        *msg_hash,
                                    const silvr_sig_t    *sig,
                                    const uint8_t        *expected_pkhash) {
    if (!pubkey || !msg_hash || !sig) return SILVR_ERR_NULL_PARAM;
    switch ((silvr_sigver_t)sig->version) {
    case SILVR_SIGVER_ECDSA_COMPACT:
    case SILVR_SIGVER_ECDSA_DER: {
        secp256k1_context *ctx =
            secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        if (!ctx) return SILVR_ERR_RAND_FAIL;
        secp256k1_pubkey raw_pub;
        if (!secp256k1_ec_pubkey_parse(ctx, &raw_pub,
                                        pubkey->data, pubkey->len)) {
            secp256k1_context_destroy(ctx);
            return SILVR_ERR_INVALID_PUBKEY;
        }
        secp256k1_ecdsa_signature raw_sig;
        int parse_ok = (sig->version == SILVR_SIGVER_ECDSA_COMPACT)
            ? secp256k1_ecdsa_signature_parse_compact(ctx, &raw_sig, sig->data)
            : secp256k1_ecdsa_signature_parse_der(ctx, &raw_sig,
                                                   sig->data, sig->len);
        if (!parse_ok) {
            secp256k1_context_destroy(ctx);
            return SILVR_ERR_INVALID_SIG;
        }
        int ok = secp256k1_ecdsa_verify(ctx, &raw_sig, msg_hash, &raw_pub);
        secp256k1_context_destroy(ctx);
        if (!ok) return SILVR_ERR_VERIFY_FAIL;
        if (expected_pkhash) {
            uint8_t actual[20];
            crypto_hash160(pubkey->data, pubkey->len, actual);
            if (memcmp(actual, expected_pkhash, 20) != 0)
                return SILVR_ERR_VERIFY_FAIL;
        }
        return SILVR_OK;
    }
    case SILVR_SIGVER_DILITHIUM2:
    case SILVR_SIGVER_FALCON512:
        return SILVR_ERR_UNKNOWN_SIGVER;
    default:
        return SILVR_ERR_UNKNOWN_SIGVER;
    }
}

/* =========================================================================
 * WIRE SERIALIZATION
 * ========================================================================= */
static int crypto_sig_serialize(const silvr_sig_t *sig,
                                 uint8_t *buf, size_t buf_len) {
    if (!sig || !buf) return SILVR_ERR_NULL_PARAM;
    if (buf_len < (size_t)(3 + sig->len)) return SILVR_ERR_BUFFER_TOO_SMALL;
    buf[0] = sig->version;
    uint16_t len_be = htons(sig->len);
    memcpy(buf + 1, &len_be, 2);
    memcpy(buf + 3, sig->data, sig->len);
    return (int)(3 + sig->len);
}

static int crypto_sig_deserialize(const uint8_t *buf, size_t buf_len,
                                   silvr_sig_t *out) {
    if (!buf || !out) return SILVR_ERR_NULL_PARAM;
    if (buf_len < 3) return SILVR_ERR_BUFFER_TOO_SMALL;
    out->version = buf[0];
    uint16_t len_be; memcpy(&len_be, buf + 1, 2);
    out->len = ntohs(len_be);
    if (out->len > SILVR_SIG_DER_MAX_LEN) return SILVR_ERR_INVALID_SIG;
    if (buf_len < (size_t)(3 + out->len))  return SILVR_ERR_BUFFER_TOO_SMALL;
    memcpy(out->data, buf + 3, out->len);
    return (int)(3 + out->len);
}

static int crypto_pubkey_serialize(const silvr_pubkey_t *pk,
                                    uint8_t *buf, size_t buf_len) {
    if (!pk || !buf) return SILVR_ERR_NULL_PARAM;
    if (buf_len < (size_t)(3 + pk->len)) return SILVR_ERR_BUFFER_TOO_SMALL;
    buf[0] = pk->addr_version;
    uint16_t len_be = htons(pk->len);
    memcpy(buf + 1, &len_be, 2);
    memcpy(buf + 3, pk->data, pk->len);
    return (int)(3 + pk->len);
}

static int crypto_pubkey_deserialize(const uint8_t *buf, size_t buf_len,
                                      silvr_pubkey_t *out) {
    if (!buf || !out) return SILVR_ERR_NULL_PARAM;
    if (buf_len < 3) return SILVR_ERR_BUFFER_TOO_SMALL;
    out->addr_version = buf[0];
    uint16_t len_be; memcpy(&len_be, buf + 1, 2);
    out->len = ntohs(len_be);
    if (out->len > SILVR_PUBKEY_UNCOMP_LEN) return SILVR_ERR_INVALID_PUBKEY;
    if (buf_len < (size_t)(3 + out->len))   return SILVR_ERR_BUFFER_TOO_SMALL;
    memcpy(out->data, buf + 3, out->len);
    return (int)(3 + out->len);
}

/* =========================================================================
 * SAFE MEMORY WIPE
 * ========================================================================= */
static inline void crypto_zeroize(void *buf, size_t len) {
#ifdef _WIN32
    SecureZeroMemory(buf, len);
#else
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len--) *p++ = 0;
#endif
}

#endif /* SILVR_CRYPTO_ABSTRACTION_H */
