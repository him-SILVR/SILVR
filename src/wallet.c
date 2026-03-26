/*
 * SILVR Wallet
 * Chain ID: 2026
 * The People's Chain
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SILVR_VERSION    1
#define ADDRESS_LENGTH   34
#define MAX_BALANCE      42000000

/* Wallet structure */
typedef struct {
    char address[ADDRESS_LENGTH + 1];
    double balance;
    uint64_t tx_count;
} SILVRWallet;

/* Create a new wallet */
SILVRWallet* wallet_create(const char* address) {
    SILVRWallet* w = malloc(sizeof(SILVRWallet));
    if (!w) return NULL;
    strncpy(w->address, address, ADDRESS_LENGTH);
    w->address[ADDRESS_LENGTH] = '\0';
    w->balance = 0.0;
    w->tx_count = 0;
    return w;
}

/* Get balance */
double wallet_get_balance(SILVRWallet* w) {
    if (!w) return 0.0;
    return w->balance;
}

/* Add mining reward */
int wallet_add_reward(SILVRWallet* w, double amount) {
    if (!w) return -1;
    if (amount <= 0) return -1;
    w->balance += amount;
    w->tx_count++;
    printf("[SILVR] Reward +%.2f SILVR | Balance: %.2f\n",
           amount, w->balance);
    return 0;
}

/* Send SILVR */
int wallet_send(SILVRWallet* w,
                const char* to_address,
                double amount) {
    if (!w) return -1;
    if (amount <= 0) return -1;
    if (amount > w->balance) {
        printf("[SILVR] ERROR: Not enough balance\n");
        return -1;
    }
    w->balance -= amount;
    w->tx_count++;
    printf("[SILVR] Sent %.2f SILVR to %s\n", amount, to_address);
    printf("[SILVR] Remaining balance: %.2f SILVR\n", w->balance);
    return 0;
}

/* Print wallet info */
void wallet_print(SILVRWallet* w) {
    if (!w) return;
    printf("=== SILVR WALLET ===\n");
    printf("Address : %s\n", w->address);
    printf("Balance : %.2f SILVR\n", w->balance);
    printf("TX Count: %llu\n",
           (unsigned long long)w->tx_count);
    printf("====================\n");
}

/* Free wallet */
void wallet_free(SILVRWallet* w) {
    if (w) free(w);
}
