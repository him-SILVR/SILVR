#include "../../include/silvr.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <openssl/sha.h>

#define MAX_CHANNELS    256
#define MAX_HOPS        8
#define HTLC_TIMEOUT    144

typedef enum {
    CHANNEL_OPEN    = 0,
    CHANNEL_ACTIVE  = 1,
    CHANNEL_CLOSING = 2,
    CHANNEL_CLOSED  = 3
} channel_status_t;

typedef struct {
    uint8_t          preimage[32];
    uint8_t          hash[32];
    silvr_amount_t   amount;
    uint32_t         expiry;
    int              settled;
} silvr_htlc_t;

typedef struct {
    uint32_t         id;
    channel_status_t status;
    silvr_address_t  local_addr;
    silvr_address_t  remote_addr;
    silvr_amount_t   local_balance;
    silvr_amount_t   remote_balance;
    silvr_amount_t   capacity;
    silvr_htlc_t     htlcs[16];
    uint32_t         htlc_count;
    uint32_t         opened_at;
} silvr_channel_t;

typedef struct {
    char             payment_hash[65];
    silvr_amount_t   amount;
    silvr_address_t  destination;
    silvr_address_t  hops[MAX_HOPS];
    uint32_t         hop_count;
    int              settled;
    uint32_t         created_at;
} silvr_invoice_t;

static silvr_channel_t channels[MAX_CHANNELS];
static uint32_t channel_count = 0;

static void compute_hash(const uint8_t *preimage,
                          uint8_t *hash_out) {
    SHA256(preimage, 32, hash_out);
}

uint32_t silvr_channel_open(
    const silvr_address_t local,
    const silvr_address_t remote,
    silvr_amount_t capacity,
    uint32_t timestamp)
{
    if (channel_count >= MAX_CHANNELS) return 0;

    silvr_channel_t *ch = &channels[channel_count];
    ch->id             = channel_count + 1;
    ch->status         = CHANNEL_ACTIVE;
    ch->capacity       = capacity;
    ch->local_balance  = capacity / 2;
    ch->remote_balance = capacity / 2;
    ch->htlc_count     = 0;
    ch->opened_at      = timestamp;

    strncpy(ch->local_addr,  local,
            sizeof(ch->local_addr)-1);
    strncpy(ch->remote_addr, remote,
            sizeof(ch->remote_addr)-1);

    printf("[LN] Channel #%u opened\n", ch->id);
    printf("     %s <-> %s\n", local, remote);
    printf("     Capacity: %.8f SILVR\n",
           (double)capacity / 1e8);

    channel_count++;
    return ch->id;
}

int silvr_htlc_add(uint32_t channel_id,
                   silvr_amount_t amount,
                   const uint8_t *payment_hash,
                   uint32_t expiry) {
    if (channel_id == 0 ||
        channel_id > channel_count) return -1;

    silvr_channel_t *ch =
        &channels[channel_id - 1];

    if (ch->status != CHANNEL_ACTIVE) return -1;
    if (ch->local_balance < amount)   return -1;
    if (ch->htlc_count >= 16)         return -1;

    silvr_htlc_t *htlc =
        &ch->htlcs[ch->htlc_count];
    memcpy(htlc->hash, payment_hash, 32);
    htlc->amount  = amount;
    htlc->expiry  = expiry;
    htlc->settled = 0;

    ch->local_balance -= amount;
    ch->htlc_count++;

    printf("[LN] HTLC added to channel #%u:"
           " %.8f SILVR\n",
           channel_id,
           (double)amount / 1e8);
    return 0;
}

int silvr_htlc_settle(uint32_t channel_id,
                      const uint8_t *preimage) {
    if (channel_id == 0 ||
        channel_id > channel_count) return -1;

    silvr_channel_t *ch =
        &channels[channel_id - 1];

    uint8_t hash[32];
    compute_hash(preimage, hash);

    for (uint32_t i = 0; i < ch->htlc_count; i++) {
        silvr_htlc_t *htlc = &ch->htlcs[i];
        if (htlc->settled) continue;
        if (memcmp(htlc->hash, hash, 32) == 0) {
            htlc->settled = 1;
            memcpy(htlc->preimage, preimage, 32);
            ch->remote_balance += htlc->amount;
            printf("[LN] HTLC settled on"
                   " channel #%u: %.8f SILVR\n",
                   channel_id,
                   (double)htlc->amount / 1e8);
            return 0;
        }
    }
    return -1;
}

int silvr_channel_close_cooperative(
    uint32_t channel_id)
{
    if (channel_id == 0 ||
        channel_id > channel_count) return -1;

    silvr_channel_t *ch =
        &channels[channel_id - 1];

    ch->status = CHANNEL_CLOSED;

    printf("[LN] Channel #%u closed cooperatively\n",
           channel_id);
    printf("     Local  final: %.8f SILVR\n",
           (double)ch->local_balance / 1e8);
    printf("     Remote final: %.8f SILVR\n",
           (double)ch->remote_balance / 1e8);
    return 0;
}

int silvr_channel_force_close(uint32_t channel_id) {
    if (channel_id == 0 ||
        channel_id > channel_count) return -1;

    silvr_channel_t *ch =
        &channels[channel_id - 1];

    ch->status = CHANNEL_CLOSED;

    printf("[LN] Channel #%u force closed\n",
           channel_id);
    return 0;
}

silvr_invoice_t silvr_create_invoice(
    const silvr_address_t destination,
    silvr_amount_t amount,
    uint32_t timestamp)
{
    silvr_invoice_t inv;
    memset(&inv, 0, sizeof(inv));

    inv.amount     = amount;
    inv.settled    = 0;
    inv.created_at = timestamp;
    strncpy(inv.destination, destination,
            sizeof(inv.destination)-1);

    /* generate payment hash */
    uint8_t preimage[32];
    for (int i = 0; i < 32; i++)
        preimage[i] = (uint8_t)(rand() & 0xFF);
    uint8_t hash[32];
    compute_hash(preimage, hash);

    for (int i = 0; i < 32; i++)
        sprintf(inv.payment_hash + i*2,
                "%02x", hash[i]);

    printf("[LN] Invoice created\n");
    printf("     Amount : %.8f SILVR\n",
           (double)amount / 1e8);
    printf("     To     : %s\n", destination);
    printf("     Hash   : %.16s...\n",
           inv.payment_hash);

    return inv;
}

void silvr_lightning_print_stats(void) {
    printf("\n[LN] === Lightning Stats ===\n");
    printf("  Channels : %u\n", channel_count);

    silvr_amount_t total = 0;
    int active = 0;
    for (uint32_t i = 0; i < channel_count; i++) {
        if (channels[i].status == CHANNEL_ACTIVE) {
            active++;
            total += channels[i].capacity;
        }
    }
    printf("  Active   : %d\n", active);
    printf("  Capacity : %.8f SILVR\n\n",
           (double)total / 1e8);
}
