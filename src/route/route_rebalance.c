#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Route partition rebalancer: redistributes partitions across nodes
 * to maintain even load distribution.
 *
 * Algorithm:
 * 1. Compute current load per node (total bytes of spans)
 * 2. Find overloaded and underloaded nodes
 * 3. Generate migration plan to move partitions from overloaded to underloaded
 * 4. Apply plan (triggers migration subsystem)
 */

typedef struct {
    uint32_t node_id;
    uint64_t total_bytes;
    uint32_t partition_count;
    uint32_t span_count;
    int32_t  load_score;  /* relative to mean, can be negative */
} spl_node_load_t;

typedef struct {
    uint32_t source_node;
    uint32_t target_node;
    uint32_t partition_id;
    uint32_t span_id;
    uint64_t bytes;
    uint32_t priority;
} spl_rebalance_move_t;

#define SPL_REBALANCE_MAX_MOVES 64

typedef struct {
    spl_node_load_t loads[SPL_MAX_NODES];
    uint32_t node_count;
    spl_rebalance_move_t moves[SPL_REBALANCE_MAX_MOVES];
    uint32_t move_count;
    uint64_t total_bytes_to_move;
    int32_t  imbalance_before;
    int32_t  imbalance_after;
} spl_rebalance_plan_t;

int spl_rebalance_compute_loads(spl_session_t *sess,
                                spl_rebalance_plan_t *plan) {
    if (!sess || !plan) return -1;
    memset(plan, 0, sizeof(*plan));

    /* Collect load per node from routes */
    for (uint16_t i = 0; i < sess->route_count; i++) {
        spl_route_entry_t *r = sess->routes[i];
        if (!r || !r->active) continue;

        /* Find or create node load entry */
        spl_node_load_t *load = NULL;
        for (uint32_t j = 0; j < plan->node_count; j++) {
            if (plan->loads[j].node_id == r->node_id) {
                load = &plan->loads[j];
                break;
            }
        }
        if (!load && plan->node_count < SPL_MAX_NODES) {
            load = &plan->loads[plan->node_count++];
            load->node_id = r->node_id;
        }
        if (!load) continue;

        load->partition_count++;
        spl_span_t *span = spl_span_registry_lookup(sess, r->span_id);
        if (span) {
            load->total_bytes += span->total_bytes;
            load->span_count++;
        }
    }

    if (plan->node_count == 0) return 0;

    /* Compute mean load */
    uint64_t total = 0;
    for (uint32_t i = 0; i < plan->node_count; i++)
        total += plan->loads[i].total_bytes;
    uint64_t mean = total / plan->node_count;

    /* Compute load scores (deviation from mean) */
    for (uint32_t i = 0; i < plan->node_count; i++) {
        plan->loads[i].load_score = (int32_t)(
            (int64_t)plan->loads[i].total_bytes - (int64_t)mean);
    }

    /* Compute imbalance score */
    int64_t sum_sq = 0;
    for (uint32_t i = 0; i < plan->node_count; i++) {
        int64_t dev = plan->loads[i].load_score;
        sum_sq += dev * dev;
    }
    plan->imbalance_before = (int32_t)(sum_sq / plan->node_count);

    return (int)plan->node_count;
}

int spl_rebalance_generate_moves(spl_rebalance_plan_t *plan) {
    if (!plan || plan->node_count < 2) return 0;

    plan->move_count = 0;

    /* Sort nodes by load (descending) */
    for (uint32_t i = 0; i < plan->node_count; i++) {
        for (uint32_t j = i + 1; j < plan->node_count; j++) {
            if (plan->loads[j].load_score > plan->loads[i].load_score) {
                spl_node_load_t tmp = plan->loads[i];
                plan->loads[i] = plan->loads[j];
                plan->loads[j] = tmp;
            }
        }
    }

    /* Generate moves from overloaded to underloaded */
    uint32_t over = 0;
    uint32_t under = plan->node_count - 1;

    while (over < under && plan->move_count < SPL_REBALANCE_MAX_MOVES) {
        if (plan->loads[over].load_score <= 0) break;
        if (plan->loads[under].load_score >= 0) break;

        /* Create a move */
        spl_rebalance_move_t *m = &plan->moves[plan->move_count++];
        m->source_node = plan->loads[over].node_id;
        m->target_node = plan->loads[under].node_id;

        /* Estimate bytes to move: half the difference */
        int64_t diff = (int64_t)plan->loads[over].load_score -
                       (int64_t)plan->loads[under].load_score;
        m->bytes = (uint64_t)(diff / 2);
        m->priority = (uint32_t)(plan->loads[over].load_score / 1024);

        plan->total_bytes_to_move += m->bytes;

        /* Adjust load scores */
        plan->loads[over].load_score -= (int32_t)m->bytes;
        plan->loads[under].load_score += (int32_t)m->bytes;

        if (plan->loads[over].load_score <= 0) over++;
        if (plan->loads[under].load_score >= 0) under--;
    }

    return (int)plan->move_count;
}

/*
 * Estimate time to complete all rebalance moves.
 */
uint32_t spl_rebalance_estimate_time(const spl_rebalance_plan_t *plan,
                                     uint32_t bandwidth_kbps) {
    if (!plan || bandwidth_kbps == 0) return UINT32_MAX;
    /* Can parallelize up to max_concurrent migrations */
    uint64_t total_ms = (plan->total_bytes_to_move * 8) / bandwidth_kbps;
    uint32_t concurrent = plan->move_count > 4 ? 4 : plan->move_count;
    if (concurrent > 0) total_ms /= concurrent;
    return total_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)total_ms;
}
