#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Channel priority scheduler: weighted fair queuing for
 * multiplexed channel operations.
 *
 * Uses deficit round-robin (DRR) algorithm for fairness
 * with per-channel priority weights.
 */

#define SPL_SCHED_MAX_CHANNELS 64

typedef struct {
    uint32_t channel_id;
    uint32_t weight;
    int32_t  deficit;
    uint32_t queue_depth;
    uint32_t bytes_sent;
    bool     active;
} spl_sched_entry_t;

typedef struct {
    spl_sched_entry_t entries[SPL_SCHED_MAX_CHANNELS];
    uint32_t          count;
    uint32_t          current_idx;
    uint32_t          quantum;  /* base quantum per round */
    uint64_t          total_bytes;
    uint32_t          round_count;
} spl_channel_scheduler_t;

int spl_sched_init(spl_channel_scheduler_t *sched, uint32_t quantum) {
    if (!sched) return -1;
    memset(sched, 0, sizeof(*sched));
    sched->quantum = quantum > 0 ? quantum : 1500;  /* default: 1 MTU */
    return 0;
}

int spl_sched_add_channel(spl_channel_scheduler_t *sched,
                          uint32_t channel_id, uint32_t weight) {
    if (!sched || sched->count >= SPL_SCHED_MAX_CHANNELS) return -1;

    spl_sched_entry_t *e = &sched->entries[sched->count++];
    e->channel_id = channel_id;
    e->weight = weight > 0 ? weight : 1;
    e->deficit = 0;
    e->queue_depth = 0;
    e->bytes_sent = 0;
    e->active = true;

    return 0;
}

int spl_sched_remove_channel(spl_channel_scheduler_t *sched,
                             uint32_t channel_id) {
    if (!sched) return -1;

    for (uint32_t i = 0; i < sched->count; i++) {
        if (sched->entries[i].channel_id == channel_id) {
            sched->entries[i] = sched->entries[sched->count - 1];
            sched->count--;
            return 0;
        }
    }
    return -1;
}

int spl_sched_enqueue(spl_channel_scheduler_t *sched,
                      uint32_t channel_id, uint32_t bytes) {
    if (!sched) return -1;

    for (uint32_t i = 0; i < sched->count; i++) {
        if (sched->entries[i].channel_id == channel_id) {
            sched->entries[i].queue_depth += bytes;
            return 0;
        }
    }
    return -1;
}

/*
 * DRR scheduling round: select next channel to send.
 * Returns channel_id or 0 if no channel is ready.
 */
uint32_t spl_sched_next(spl_channel_scheduler_t *sched) {
    if (!sched || sched->count == 0) return 0;

    uint32_t start = sched->current_idx;
    uint32_t scanned = 0;

    while (scanned < sched->count) {
        spl_sched_entry_t *e = &sched->entries[sched->current_idx];
        sched->current_idx = (sched->current_idx + 1) % sched->count;
        scanned++;

        if (!e->active || e->queue_depth == 0) continue;

        /* Add weighted quantum to deficit */
        e->deficit += (int32_t)(sched->quantum * e->weight);

        if (e->deficit > 0) {
            /* Can send: debit from queue and deficit */
            uint32_t to_send = e->queue_depth;
            if (to_send > (uint32_t)e->deficit)
                to_send = (uint32_t)e->deficit;

            e->queue_depth -= to_send;
            e->deficit -= (int32_t)to_send;
            e->bytes_sent += to_send;
            sched->total_bytes += to_send;

            if (e->queue_depth == 0)
                e->deficit = 0;

            return e->channel_id;
        }
    }

    sched->round_count++;
    (void)start;
    return 0;
}

/*
 * Compute fairness index (Jain's fairness index * 1000).
 * 1000 = perfectly fair, lower = less fair.
 */
int spl_sched_fairness_index(const spl_channel_scheduler_t *sched) {
    if (!sched || sched->count < 2) return 1000;

    /* Jain's index = (sum(xi))^2 / (n * sum(xi^2)) */
    uint64_t sum = 0, sum_sq = 0;
    uint32_t n = 0;

    for (uint32_t i = 0; i < sched->count; i++) {
        if (!sched->entries[i].active) continue;
        uint64_t normalized = (uint64_t)sched->entries[i].bytes_sent;
        if (sched->entries[i].weight > 0)
            normalized /= sched->entries[i].weight;
        sum += normalized;
        sum_sq += normalized * normalized;
        n++;
    }

    if (n == 0 || sum_sq == 0) return 1000;
    return (int)((sum * sum * 1000) / (n * sum_sq));
}
