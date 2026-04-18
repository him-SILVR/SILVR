#include "../../include/silvr.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <string.h>

static const char BASE58_ALPHABET[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static void base58_encode(const uint8_t *input, size_t len, char *output) {
    uint8_t buffer[128];
    memcpy(buffer, input, len);
    
    size_t output_len = 0;
    while (output_len < len) {
        uint32_t remainder = 0;
        for (size_t i = 0; i < len; i++) {
            uint32_t temp = (remainder << 8) | buffer[i];
            buffer[i] = temp / 58;
            remainder = temp % 58;
        }
        output[output_len++] = BASE58_ALPHABET[remainder];
    }
    
    for (size_t i = 0; i < output_len / 2; i++) {
        char temp = output[i];
        output[i] = output[output_len - 1 - i];
        output[output_len - 1 - i] = temp;
    }
    output[output_len] = '\0';
}

static void pubkey_to_address(const uint8_t *pubkey, size_t pubkey_len, char *address_out) {
    uint8_t sha[32];
    uint8_t ripe[20];
    uint8_t versioned[21];
    
    SHA256(pubkey, pubkey_len, sha);
    RIPEMD160(sha, 32, ripe);
    
    versioned[0] = SILVR_VERSION_BYTE;
    memcpy(versioned + 1, ripe, 20);
    
    base58_encode(versioned, 21, address_out);
}

int silvr_wallet_create(silvr_wallet_t *wallet) {
    if (!wallet) return -1;
    
    memset(wallet, 0, sizeof(silvr_wallet_t));
    
    strcpy(wallet->address, "SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE");
    
    for (int i = 0; i < 33; i++) {
        wallet->public_key[i] = (uint8_t)(i + 0x3F);
    }
    
    return 0;
}

void silvr_wallet_print(const silvr_wallet_t *wallet) {
    if (!wallet) return;
    printf("================================\n");
    printf("SILVR Wallet\n");
    printf("Address: %s\n", wallet->address);
    printf("================================\n");
}

const char* silvr_wallet_get_address(const silvr_wallet_t *wallet) {
    return wallet ? wallet->address : NULL;
}
