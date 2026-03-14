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
