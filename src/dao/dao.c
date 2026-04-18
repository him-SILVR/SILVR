#include "../../include/silvr.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define MAX_PROPOSALS     256
#define MAX_VOTERS        1024
#define QUORUM_PCT        10
#define MAJORITY_PCT      51
#define VOTING_WINDOW     5184000
#define EXEC_TIMELOCK     604800

typedef enum {
    PROPOSAL_PARAMETER = 0,
    PROPOSAL_TREASURY  = 1,
    PROPOSAL_UPGRADE   = 2,
    PROPOSAL_SLASH     = 3
} proposal_type_t;

typedef enum {
    STATUS_ACTIVE   = 0,
    STATUS_PASSED   = 1,
    STATUS_FAILED   = 2,
    STATUS_EXECUTED = 3
} proposal_status_t;

typedef struct {
    uint32_t           id;
    proposal_type_t    type;
    proposal_status_t  status;
    char               title[128];
    char               description[512];
    silvr_address_t    proposer;
    uint32_t           created_at;
    uint32_t           voting_ends;
    uint64_t           votes_yes;
    uint64_t           votes_no;
    uint64_t           total_weight;
    silvr_amount_t     treasury_amount;
    silvr_address_t    treasury_recipient;
} silvr_proposal_t;

typedef struct {
    silvr_address_t  address;
    silvr_amount_t   staked;
    uint64_t         vote_weight;
    uint32_t         lock_until;
} silvr_staker_t;

static silvr_proposal_t proposals[MAX_PROPOSALS];
static silvr_staker_t   stakers[MAX_VOTERS];
static uint32_t proposal_count = 0;
static uint32_t staker_count   = 0;
static silvr_amount_t treasury_balance = 0;

/* sqrt weighted voting */
uint64_t silvr_vote_weight(silvr_amount_t staked_grains) {
    if (staked_grains == 0) return 0;
    return (uint64_t)sqrt((double)staked_grains);
}

int silvr_stake(const silvr_address_t address,
                silvr_amount_t amount,
                uint32_t current_height) {
    if (amount < SILVR_MIN_STAKE) {
        printf("[DAO] Stake below minimum: %.8f SILVR\n",
               (double)amount / 1e8);
        return -1;
    }

    /* find existing staker */
    for (uint32_t i = 0; i < staker_count; i++) {
        if (strcmp(stakers[i].address, address) == 0) {
            stakers[i].staked      += amount;
            stakers[i].vote_weight  =
                silvr_vote_weight(stakers[i].staked);
            stakers[i].lock_until   =
                current_height + 1008;
            printf("[DAO] Updated stake: %s — %.8f SILVR"
                   " weight=%llu\n",
                   address,
                   (double)stakers[i].staked / 1e8,
                   (unsigned long long)
                   stakers[i].vote_weight);
            return 0;
        }
    }

    /* new staker */
    if (staker_count >= MAX_VOTERS) return -1;
    silvr_staker_t *s = &stakers[staker_count++];
    strncpy(s->address, address, sizeof(s->address)-1);
    s->staked      = amount;
    s->vote_weight = silvr_vote_weight(amount);
    s->lock_until  = current_height + 1008;

    printf("[DAO] New staker: %s — %.8f SILVR"
           " weight=%llu\n",
           address,
           (double)amount / 1e8,
           (unsigned long long)s->vote_weight);
    return 0;
}

uint32_t silvr_dao_propose(
    const silvr_address_t proposer,
    proposal_type_t type,
    const char *title,
    const char *description,
    silvr_amount_t treasury_amount,
    const silvr_address_t recipient,
    uint32_t timestamp)
{
    if (proposal_count >= MAX_PROPOSALS) return 0;

    silvr_proposal_t *p = &proposals[proposal_count];
    p->id           = proposal_count + 1;
    p->type         = type;
    p->status       = STATUS_ACTIVE;
    p->created_at   = timestamp;
    p->voting_ends  = timestamp + VOTING_WINDOW;
    p->votes_yes    = 0;
    p->votes_no     = 0;
    p->total_weight = 0;
    p->treasury_amount = treasury_amount;

    strncpy(p->proposer,     proposer,
            sizeof(p->proposer)-1);
    strncpy(p->title,        title,
            sizeof(p->title)-1);
    strncpy(p->description,  description,
            sizeof(p->description)-1);
    if (recipient)
        strncpy(p->treasury_recipient, recipient,
                sizeof(p->treasury_recipient)-1);

    printf("[DAO] Proposal #%u created: %s\n",
           p->id, p->title);
    proposal_count++;
    return p->id;
}

int silvr_dao_vote(uint32_t proposal_id,
                   const silvr_address_t voter,
                   int vote_yes,
                   uint32_t timestamp) {
    if (proposal_id == 0 ||
        proposal_id > proposal_count) return -1;

    silvr_proposal_t *p =
        &proposals[proposal_id - 1];

    if (p->status != STATUS_ACTIVE) {
        printf("[DAO] Proposal not active\n");
        return -1;
    }
    if (timestamp > p->voting_ends) {
        printf("[DAO] Voting period ended\n");
        return -1;
    }

    /* find voter weight */
    uint64_t weight = 0;
    for (uint32_t i = 0; i < staker_count; i++) {
        if (strcmp(stakers[i].address, voter) == 0) {
            weight = stakers[i].vote_weight;
            break;
        }
    }
    if (weight == 0) {
        printf("[DAO] Voter has no stake\n");
        return -1;
    }

    if (vote_yes)
        p->votes_yes += weight;
    else
        p->votes_no  += weight;
    p->total_weight  += weight;

    printf("[DAO] Vote cast on proposal #%u:"
           " %s weight=%llu\n",
           proposal_id,
           vote_yes ? "YES" : "NO",
           (unsigned long long)weight);
    return 0;
}

int silvr_dao_finalise(uint32_t proposal_id,
                       uint32_t timestamp) {
    if (proposal_id == 0 ||
        proposal_id > proposal_count) return -1;

    silvr_proposal_t *p =
        &proposals[proposal_id - 1];

    if (p->status != STATUS_ACTIVE) return -1;
    if (timestamp < p->voting_ends)  return -1;

    /* calculate total staked weight */
    uint64_t total_staked = 0;
    for (uint32_t i = 0; i < staker_count; i++)
        total_staked += stakers[i].vote_weight;

    /* check quorum */
    uint64_t quorum_needed =
        (total_staked * QUORUM_PCT) / 100;
    if (p->total_weight < quorum_needed) {
        p->status = STATUS_FAILED;
        printf("[DAO] Proposal #%u FAILED:"
               " quorum not reached\n",
               proposal_id);
        return 0;
    }

    /* check majority */
    uint64_t decisive = p->votes_yes + p->votes_no;
    if (decisive == 0) {
        p->status = STATUS_FAILED;
        return 0;
    }

    uint64_t yes_pct =
        (p->votes_yes * 100) / decisive;
    if (yes_pct >= MAJORITY_PCT) {
        p->status = STATUS_PASSED;
        printf("[DAO] Proposal #%u PASSED:"
               " %llu%% yes\n",
               proposal_id,
               (unsigned long long)yes_pct);
    } else {
        p->status = STATUS_FAILED;
        printf("[DAO] Proposal #%u FAILED:"
               " %llu%% yes\n",
               proposal_id,
               (unsigned long long)yes_pct);
    }
    return 0;
}

void silvr_treasury_deposit(silvr_amount_t amount) {
    treasury_balance += amount;
}

silvr_amount_t silvr_treasury_get_balance(void) {
    return treasury_balance;
}

void silvr_dao_print_stats(void) {
    printf("\n[DAO] === Stats ===\n");
    printf("  Proposals  : %u\n", proposal_count);
    printf("  Stakers    : %u\n", staker_count);
    printf("  Treasury   : %.8f SILVR\n",
           (double)treasury_balance / 1e8);

    uint64_t total = 0;
    for (uint32_t i = 0; i < staker_count; i++)
        total += stakers[i].staked;
    printf("  Total staked: %.8f SILVR\n\n",
           (double)total / 1e8);
}
