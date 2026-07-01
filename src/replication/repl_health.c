#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Replica health scoring and quorum selection.
 * Each replica gets a health score based on:
 *   - Sync lag (lower is better)
 *   - Response latency (lower is better)
 *   - Error rate (lower is better)
 *   - Consecutive failures (lower is better)
 */

typedef struct {
    uint32_t node_id;
    uint64_t last_sync_offset;
    uint64_t leader_offset;
    uint32_t latency_avg_us;
    uint32_t latency_p99_us;
    uint32_t error_count;
    uint32_t success_count;
    uint32_t consecutive_failures;
    uint32_t health_score;  /* 0-1000, higher is better */
    bool     is_leader;
} spl_replica_health_t;

/*
 * Exponentially weighted moving average (EWMA) for latency tracking.
 * Alpha = 2/(N+1) where N = window size.
 * We use fixed-point math: alpha_fp = alpha * 1024.
 */
static uint32_t ewma_update(uint32_t current, uint32_t sample,
                            uint32_t alpha_fp) {
    /* new = alpha * sample + (1 - alpha) * current */
    uint64_t weighted_sample = (uint64_t)alpha_fp * sample;
    uint64_t weighted_current = (uint64_t)(1024 - alpha_fp) * current;
    return (uint32_t)((weighted_sample + weighted_current) / 1024);
}

int spl_repl_compute_health(spl_replica_health_t *health) {
    if (!health) return -1;

    uint32_t score = 1000;

    /* Sync lag penalty: lose 1 point per 1KB of lag, capped at 400 */
    if (health->leader_offset > health->last_sync_offset) {
        uint64_t lag = health->leader_offset - health->last_sync_offset;
        uint32_t lag_penalty = (uint32_t)(lag / 1024);
        if (lag_penalty > 400) lag_penalty = 400;
        score -= lag_penalty;
    }

    /* Latency penalty: lose 1 point per 100us avg, capped at 200 */
    uint32_t lat_penalty = health->latency_avg_us / 100;
    if (lat_penalty > 200) lat_penalty = 200;
    score -= lat_penalty;

    /* Error rate penalty: lose 2 points per percent error rate, capped at 200 */
    uint32_t total = health->error_count + health->success_count;
    if (total > 0) {
        uint32_t error_pct = (health->error_count * 100) / total;
        uint32_t err_penalty = error_pct * 2;
        if (err_penalty > 200) err_penalty = 200;
        score -= err_penalty;
    }

    /* Consecutive failure penalty: lose 50 per consecutive failure, capped at 200 */
    uint32_t fail_penalty = health->consecutive_failures * 50;
    if (fail_penalty > 200) fail_penalty = 200;
    score -= fail_penalty;

    health->health_score = score;
    return (int)score;
}

/*
 * Select the best quorum of N replicas from available set.
 * Sorts by health score and selects top N.
 */
int spl_repl_select_quorum(spl_replica_health_t *replicas, uint8_t count,
                           uint8_t quorum_size,
                           uint32_t *out_node_ids) {
    if (!replicas || !out_node_ids || count == 0) return -1;
    if (quorum_size > count) return -1;

    /* Compute health scores */
    for (uint8_t i = 0; i < count; i++) {
        spl_repl_compute_health(&replicas[i]);
    }

    /* Selection sort by health score (descending) */
    for (uint8_t i = 0; i < quorum_size; i++) {
        uint8_t best = i;
        for (uint8_t j = i + 1; j < count; j++) {
            if (replicas[j].health_score > replicas[best].health_score)
                best = j;
        }
        if (best != i) {
            spl_replica_health_t tmp = replicas[i];
            replicas[i] = replicas[best];
            replicas[best] = tmp;
        }
        out_node_ids[i] = replicas[i].node_id;
    }

    return 0;
}

/*
 * Compute replication factor recommendation based on data criticality
 * and available node count.
 */
int spl_repl_recommend_factor(uint32_t node_count, uint32_t criticality_level,
                              uint64_t data_size_bytes) {
    /* Base recommendation: floor(node_count / 2) + 1, capped at 5 */
    int base = (int)(node_count / 2) + 1;
    if (base > 5) base = 5;

    /* Adjust for criticality */
    if (criticality_level >= 3) base++;  /* high criticality */
    if (criticality_level >= 5) base++;  /* mission critical */

    /* Reduce for very large data (cost consideration) */
    if (data_size_bytes > (uint64_t)1024 * 1024 * 1024 * 10) {
        base--;  /* >10GB, reduce by 1 */
    }

    if (base < 1) base = 1;
    if ((uint32_t)base > node_count) base = (int)node_count;

    return base;
}

/*
 * Detect split-brain condition: checks if any two subsets of replicas
 * have conflicting leaders.
 */
int spl_repl_detect_split_brain(spl_replica_health_t *replicas, uint8_t count) {
    if (!replicas || count < 2) return 0;

    uint32_t leader_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (replicas[i].is_leader) leader_count++;
    }

    /* More than one leader indicates split-brain */
    return leader_count > 1 ? 1 : 0;
}
