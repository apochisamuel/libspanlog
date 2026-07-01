#include "../common/types.h"

/*
 * Backpressure fairness monitoring: detects and corrects
 * credit allocation imbalances across partitions.
 */

typedef struct {
    uint32_t partition_id;
    int32_t  credit_balance;
    uint32_t sends;
    uint32_t drops;
    uint64_t bytes_total;
    uint32_t weight;
} spl_bp_monitor_entry_t;

#define SPL_MONITOR_MAX 128

typedef struct {
    spl_bp_monitor_entry_t entries[SPL_MONITOR_MAX];
    uint32_t count;
    uint32_t sample_window;
    uint32_t rebalance_threshold;  /* CV * 1000 above which to rebalance */
} spl_bp_monitor_t;

int spl_bp_monitor_init(spl_bp_monitor_t *mon, uint32_t threshold) {
    if (!mon) return -1;
    memset(mon, 0, sizeof(*mon));
    mon->rebalance_threshold = threshold > 0 ? threshold : 200;
    mon->sample_window = 100;
    return 0;
}

int spl_bp_monitor_record(spl_bp_monitor_t *mon, uint32_t partition_id,
                          int32_t credits, uint32_t bytes, bool dropped) {
    if (!mon) return -1;

    spl_bp_monitor_entry_t *entry = NULL;
    for (uint32_t i = 0; i < mon->count; i++) {
        if (mon->entries[i].partition_id == partition_id) {
            entry = &mon->entries[i];
            break;
        }
    }

    if (!entry && mon->count < SPL_MONITOR_MAX) {
        entry = &mon->entries[mon->count++];
        entry->partition_id = partition_id;
        entry->weight = 1;
    }
    if (!entry) return -1;

    entry->credit_balance = credits;
    entry->bytes_total += bytes;
    if (dropped) entry->drops++; else entry->sends++;

    return 0;
}

/*
 * Compute Jain's fairness index across partitions.
 * Returns 0-1000 (1000 = perfectly fair).
 */
int spl_bp_monitor_fairness(const spl_bp_monitor_t *mon) {
    if (!mon || mon->count < 2) return 1000;

    uint64_t sum = 0, sum_sq = 0;
    for (uint32_t i = 0; i < mon->count; i++) {
        uint64_t throughput = mon->entries[i].sends;
        if (mon->entries[i].weight > 0)
            throughput /= mon->entries[i].weight;
        sum += throughput;
        sum_sq += throughput * throughput;
    }

    if (sum_sq == 0) return 1000;
    return (int)((sum * sum * 1000) / (mon->count * sum_sq));
}

/*
 * Detect starvation: a partition that gets < 10% of its fair share.
 * Returns partition_id of starved partition, or 0 if none.
 */
uint32_t spl_bp_monitor_detect_starvation(const spl_bp_monitor_t *mon) {
    if (!mon || mon->count < 2) return 0;

    uint64_t total_sends = 0;
    for (uint32_t i = 0; i < mon->count; i++)
        total_sends += mon->entries[i].sends;

    if (total_sends == 0) return 0;
    uint64_t fair_share = total_sends / mon->count;
    uint64_t starvation_threshold = fair_share / 10;

    for (uint32_t i = 0; i < mon->count; i++) {
        if (mon->entries[i].sends < starvation_threshold &&
            mon->entries[i].drops > 0) {
            return mon->entries[i].partition_id;
        }
    }
    return 0;
}

/*
 * Compute credit rebalance deltas to improve fairness.
 * out_deltas[i] = how many credits to add (positive) or remove (negative)
 * from partition i to improve balance.
 */
int spl_bp_monitor_compute_rebalance(const spl_bp_monitor_t *mon,
                                     int32_t *out_deltas,
                                     uint32_t max_entries) {
    if (!mon || !out_deltas) return -1;

    /* Compute target distribution: proportional to weight */
    uint32_t total_weight = 0;
    int32_t total_credits = 0;
    for (uint32_t i = 0; i < mon->count && i < max_entries; i++) {
        total_weight += mon->entries[i].weight;
        total_credits += mon->entries[i].credit_balance;
    }
    if (total_weight == 0) return 0;

    for (uint32_t i = 0; i < mon->count && i < max_entries; i++) {
        int32_t target = (total_credits * (int32_t)mon->entries[i].weight) /
                         (int32_t)total_weight;
        out_deltas[i] = target - mon->entries[i].credit_balance;
    }

    return (int)mon->count;
}
