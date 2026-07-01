#include "../common/types.h"

/*
 * Session capability negotiation state machine and
 * capability dependency matrix.
 *
 * Some capabilities depend on others:
 *   STREAM requires CHANNEL
 *   CURSOR requires STREAM
 *   MIGRATION requires REPLICATION
 *   CHECKPOINT requires RETENTION
 *   CRYPTO is independent
 *   BACKPRESSURE requires CHANNEL
 */

typedef struct {
    uint32_t capability;
    uint32_t dependencies;
    const char *name;
} spl_cap_entry_t;

static const spl_cap_entry_t cap_table[] = {
    { SPL_CAP_CHANNEL,      0,                                       "channel" },
    { SPL_CAP_STREAM,       SPL_CAP_CHANNEL,                         "stream" },
    { SPL_CAP_CURSOR,       SPL_CAP_STREAM | SPL_CAP_CHANNEL,        "cursor" },
    { SPL_CAP_REPLICATION,  0,                                       "replication" },
    { SPL_CAP_MIGRATION,    SPL_CAP_REPLICATION,                     "migration" },
    { SPL_CAP_CHECKPOINT,   SPL_CAP_RETENTION,                       "checkpoint" },
    { SPL_CAP_RETENTION,    0,                                       "retention" },
    { SPL_CAP_BACKPRESSURE, SPL_CAP_CHANNEL,                         "backpressure" },
    { SPL_CAP_CRYPTO,       0,                                       "crypto" },
};

#define SPL_CAP_TABLE_SIZE (sizeof(cap_table) / sizeof(cap_table[0]))

/*
 * Resolve capability dependencies: given requested capabilities,
 * ensure all dependencies are also included.
 */
uint32_t spl_cap_resolve_deps(uint32_t requested) {
    uint32_t resolved = requested;
    bool changed = true;

    /* Fixed-point iteration until no new deps are added */
    while (changed) {
        changed = false;
        for (size_t i = 0; i < SPL_CAP_TABLE_SIZE; i++) {
            if ((resolved & cap_table[i].capability) &&
                (resolved & cap_table[i].dependencies) != cap_table[i].dependencies) {
                resolved |= cap_table[i].dependencies;
                changed = true;
            }
        }
    }
    return resolved;
}

/*
 * Validate that a set of capabilities is consistent
 * (all dependencies satisfied).
 */
int spl_cap_validate(uint32_t capabilities) {
    for (size_t i = 0; i < SPL_CAP_TABLE_SIZE; i++) {
        if ((capabilities & cap_table[i].capability) &&
            (capabilities & cap_table[i].dependencies) != cap_table[i].dependencies) {
            return -1;
        }
    }
    return 0;
}

/*
 * Compute the intersection of two capability sets.
 * Used during negotiation to find common capabilities.
 */
uint32_t spl_cap_negotiate(uint32_t offered, uint32_t requested) {
    uint32_t common = offered & requested;
    /* Resolve dependencies on the common set */
    return spl_cap_resolve_deps(common);
}

/*
 * Count active capabilities in a bitmask.
 */
int spl_cap_count(uint32_t capabilities) {
    int count = 0;
    for (size_t i = 0; i < SPL_CAP_TABLE_SIZE; i++) {
        if (capabilities & cap_table[i].capability) count++;
    }
    return count;
}

/*
 * Get capability name by flag.
 */
const char *spl_cap_name(uint32_t cap_flag) {
    for (size_t i = 0; i < SPL_CAP_TABLE_SIZE; i++) {
        if (cap_table[i].capability == cap_flag)
            return cap_table[i].name;
    }
    return "unknown";
}

/*
 * Negotiation state machine.
 */
typedef enum {
    SPL_NEG_INITIAL     = 0,
    SPL_NEG_OFFERED     = 1,
    SPL_NEG_COUNTERED   = 2,
    SPL_NEG_ACCEPTED    = 3,
    SPL_NEG_REJECTED    = 4
} spl_neg_state_t;

typedef struct {
    spl_neg_state_t state;
    uint32_t        local_offer;
    uint32_t        remote_offer;
    uint32_t        agreed;
    uint8_t         round;
    uint8_t         max_rounds;
} spl_neg_ctx_t;

int spl_neg_init(spl_neg_ctx_t *ctx, uint32_t local_caps) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->local_offer = spl_cap_resolve_deps(local_caps);
    ctx->max_rounds = 3;
    ctx->state = SPL_NEG_INITIAL;
    return 0;
}

int spl_neg_send_offer(spl_neg_ctx_t *ctx) {
    if (!ctx || ctx->state == SPL_NEG_ACCEPTED) return -1;
    ctx->state = SPL_NEG_OFFERED;
    ctx->round++;
    return 0;
}

int spl_neg_receive_offer(spl_neg_ctx_t *ctx, uint32_t remote_caps) {
    if (!ctx) return -1;
    ctx->remote_offer = spl_cap_resolve_deps(remote_caps);

    uint32_t common = spl_cap_negotiate(ctx->local_offer, ctx->remote_offer);
    if (common == ctx->local_offer || ctx->round >= ctx->max_rounds) {
        ctx->agreed = common;
        ctx->state = SPL_NEG_ACCEPTED;
    } else {
        ctx->state = SPL_NEG_COUNTERED;
        ctx->local_offer = common;
    }
    return 0;
}

bool spl_neg_is_complete(const spl_neg_ctx_t *ctx) {
    return ctx && (ctx->state == SPL_NEG_ACCEPTED || ctx->state == SPL_NEG_REJECTED);
}
