#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Multi-tier retention policy engine.
 * Supports hot/warm/cold storage tiers with automatic data movement
 * based on age and access patterns.
 */

typedef enum {
    SPL_TIER_HOT   = 0,
    SPL_TIER_WARM  = 1,
    SPL_TIER_COLD  = 2,
    SPL_TIER_ARCHIVE = 3,
    SPL_TIER_DELETE = 4
} spl_storage_tier_t;

typedef struct {
    spl_storage_tier_t tier;
    uint32_t min_age_epochs;
    uint32_t max_age_epochs;
    uint64_t size_threshold_bytes;
    uint32_t access_threshold;  /* min accesses to stay in tier */
    uint32_t compression_level; /* 0=none, 1=fast, 2=balanced, 3=max */
} spl_tier_policy_t;

typedef struct {
    spl_tier_policy_t tiers[5];
    uint8_t tier_count;
    uint32_t evaluation_interval_epochs;
} spl_tier_config_t;

int spl_tier_config_init(spl_tier_config_t *config) {
    if (!config) return -1;
    memset(config, 0, sizeof(*config));

    /* Default tier configuration */
    config->tiers[0] = (spl_tier_policy_t){
        .tier = SPL_TIER_HOT, .min_age_epochs = 0, .max_age_epochs = 5,
        .size_threshold_bytes = 0, .access_threshold = 10,
        .compression_level = 0
    };
    config->tiers[1] = (spl_tier_policy_t){
        .tier = SPL_TIER_WARM, .min_age_epochs = 5, .max_age_epochs = 20,
        .size_threshold_bytes = 1024 * 1024,
        .access_threshold = 3, .compression_level = 1
    };
    config->tiers[2] = (spl_tier_policy_t){
        .tier = SPL_TIER_COLD, .min_age_epochs = 20, .max_age_epochs = 100,
        .size_threshold_bytes = 1024 * 1024 * 10,
        .access_threshold = 0, .compression_level = 2
    };
    config->tiers[3] = (spl_tier_policy_t){
        .tier = SPL_TIER_ARCHIVE, .min_age_epochs = 100,
        .max_age_epochs = UINT32_MAX,
        .size_threshold_bytes = 0, .access_threshold = 0,
        .compression_level = 3
    };
    config->tiers[4] = (spl_tier_policy_t){
        .tier = SPL_TIER_DELETE, .min_age_epochs = UINT32_MAX,
        .max_age_epochs = UINT32_MAX, .size_threshold_bytes = 0,
        .access_threshold = 0, .compression_level = 0
    };
    config->tier_count = 5;
    config->evaluation_interval_epochs = 5;

    return 0;
}

/*
 * Evaluate which tier a span should be in based on current metrics.
 */
spl_storage_tier_t spl_tier_evaluate(const spl_tier_config_t *config,
                                     uint32_t age_epochs,
                                     uint64_t total_bytes,
                                     uint32_t recent_accesses) {
    if (!config) return SPL_TIER_HOT;

    for (uint8_t i = 0; i < config->tier_count; i++) {
        const spl_tier_policy_t *t = &config->tiers[i];

        if (age_epochs >= t->min_age_epochs && age_epochs < t->max_age_epochs) {
            /* Check access threshold */
            if (t->access_threshold > 0 && recent_accesses >= t->access_threshold) {
                return t->tier;  /* actively accessed, stays in this tier */
            }
            if (t->access_threshold == 0) {
                return t->tier;  /* no access requirement */
            }
            /* Below access threshold: fall through to next tier */
        }
    }

    return SPL_TIER_COLD;  /* default */
}

/*
 * Compute tier transition actions for all spans in a session.
 */
typedef struct {
    uint32_t span_id;
    spl_storage_tier_t current_tier;
    spl_storage_tier_t target_tier;
    bool needs_compression;
    bool needs_decompression;
    bool needs_deletion;
} spl_tier_action_t;

int spl_tier_plan_transitions(spl_session_t *sess,
                              const spl_tier_config_t *config,
                              spl_tier_action_t *actions,
                              uint32_t max_actions) {
    if (!sess || !config || !actions) return -1;

    uint32_t count = 0;
    for (uint16_t i = 0; i < sess->span_count && count < max_actions; i++) {
        spl_span_t *span = sess->spans[i];
        if (!span || span->state != SPL_LIFECYCLE_ACTIVE) continue;

        uint32_t age = sess->epoch - span->epoch_created;
        spl_storage_tier_t target = spl_tier_evaluate(
            config, age, span->total_bytes, 0);

        /* Determine current tier (simplified: based on compression state) */
        spl_storage_tier_t current = SPL_TIER_HOT;
        if (span->segment_count > 0 && span->segments[0] &&
            span->segments[0]->compacted) {
            current = SPL_TIER_WARM;
        }

        if (target != current) {
            spl_tier_action_t *a = &actions[count++];
            a->span_id = span->span_id;
            a->current_tier = current;
            a->target_tier = target;
            a->needs_compression = (target > current);
            a->needs_decompression = (target < current);
            a->needs_deletion = (target == SPL_TIER_DELETE);
        }
    }

    return (int)count;
}

/*
 * Estimate storage savings from tier transitions.
 */
uint64_t spl_tier_estimate_savings(const spl_tier_action_t *actions,
                                   uint32_t action_count,
                                   spl_session_t *sess) {
    if (!actions || !sess) return 0;

    uint64_t savings = 0;
    for (uint32_t i = 0; i < action_count; i++) {
        const spl_tier_action_t *a = &actions[i];
        spl_span_t *span = spl_span_registry_lookup(sess, a->span_id);
        if (!span) continue;

        if (a->needs_deletion) {
            savings += span->total_bytes;
        } else if (a->needs_compression) {
            /* Estimate compression ratio based on tier */
            uint32_t ratio = 100;
            switch (a->target_tier) {
            case SPL_TIER_WARM: ratio = 70; break;  /* 30% savings */
            case SPL_TIER_COLD: ratio = 50; break;   /* 50% savings */
            case SPL_TIER_ARCHIVE: ratio = 30; break; /* 70% savings */
            default: break;
            }
            savings += span->total_bytes * (100 - ratio) / 100;
        }
    }

    return savings;
}
