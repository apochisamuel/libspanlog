#include "../common/types.h"

/*
 * Route metrics collection and reporting.
 * Tracks per-route latency, throughput, and error rates.
 */

#define SPL_ROUTE_METRICS_MAX 128
#define SPL_ROUTE_LATENCY_SAMPLES 32

typedef struct {
    uint32_t route_id;
    uint32_t span_id;
    uint32_t node_id;
    uint64_t bytes_routed;
    uint32_t requests_total;
    uint32_t requests_failed;
    uint32_t latency_samples[SPL_ROUTE_LATENCY_SAMPLES];
    uint8_t  latency_idx;
    uint8_t  latency_count;
    uint32_t latency_avg_us;
    uint32_t latency_p50_us;
    uint32_t latency_p99_us;
    bool     active;
} spl_route_metrics_t;

typedef struct {
    spl_route_metrics_t routes[SPL_ROUTE_METRICS_MAX];
    uint32_t count;
} spl_route_metrics_store_t;

int spl_route_metrics_init(spl_route_metrics_store_t *store) {
    if (!store) return -1;
    memset(store, 0, sizeof(*store));
    return 0;
}

int spl_route_metrics_record(spl_route_metrics_store_t *store,
                             uint32_t route_id, uint32_t span_id,
                             uint32_t node_id, uint64_t bytes,
                             uint32_t latency_us, bool failed) {
    if (!store) return -1;

    /* Find or create entry */
    spl_route_metrics_t *m = NULL;
    for (uint32_t i = 0; i < store->count; i++) {
        if (store->routes[i].route_id == route_id) {
            m = &store->routes[i];
            break;
        }
    }
    if (!m && store->count < SPL_ROUTE_METRICS_MAX) {
        m = &store->routes[store->count++];
        m->route_id = route_id;
        m->span_id = span_id;
        m->node_id = node_id;
        m->active = true;
    }
    if (!m) return -1;

    m->bytes_routed += bytes;
    m->requests_total++;
    if (failed) m->requests_failed++;

    /* Record latency sample */
    m->latency_samples[m->latency_idx] = latency_us;
    m->latency_idx = (m->latency_idx + 1) % SPL_ROUTE_LATENCY_SAMPLES;
    if (m->latency_count < SPL_ROUTE_LATENCY_SAMPLES) m->latency_count++;

    /* Update average */
    uint64_t sum = 0;
    for (uint8_t i = 0; i < m->latency_count; i++)
        sum += m->latency_samples[i];
    m->latency_avg_us = (uint32_t)(sum / m->latency_count);

    /* Compute percentiles via selection */
    uint32_t sorted[SPL_ROUTE_LATENCY_SAMPLES];
    memcpy(sorted, m->latency_samples, m->latency_count * sizeof(uint32_t));
    for (uint8_t i = 0; i < m->latency_count; i++) {
        for (uint8_t j = i + 1; j < m->latency_count; j++) {
            if (sorted[j] < sorted[i]) {
                uint32_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    if (m->latency_count > 0) {
        m->latency_p50_us = sorted[m->latency_count / 2];
        m->latency_p99_us = sorted[(m->latency_count * 99) / 100];
    }

    return 0;
}

int spl_route_metrics_error_rate(const spl_route_metrics_t *m) {
    if (!m || m->requests_total == 0) return 0;
    return (int)((m->requests_failed * 1000) / m->requests_total);
}

int spl_route_metrics_throughput_kbps(const spl_route_metrics_t *m,
                                     uint32_t elapsed_ms) {
    if (!m || elapsed_ms == 0) return 0;
    return (int)((m->bytes_routed * 8) / elapsed_ms);
}

uint32_t spl_route_metrics_best_route(const spl_route_metrics_store_t *store,
                                      uint32_t span_id) {
    if (!store) return 0;
    uint32_t best = 0;
    uint32_t best_latency = UINT32_MAX;
    for (uint32_t i = 0; i < store->count; i++) {
        const spl_route_metrics_t *m = &store->routes[i];
        if (!m->active || m->span_id != span_id) continue;
        if (m->latency_avg_us < best_latency) {
            best_latency = m->latency_avg_us;
            best = m->route_id;
        }
    }
    return best;
}

/*
 * Aggregate metrics across all routes for a given node.
 * Returns combined throughput, latency, and error statistics.
 */
int spl_route_metrics_aggregate(const spl_route_metrics_store_t *store,
                                uint32_t node_id,
                                uint64_t *total_bytes,
                                uint32_t *avg_latency,
                                uint32_t *error_rate) {
    if (!store || !total_bytes || !avg_latency || !error_rate) return -1;

    *total_bytes = 0;
    *avg_latency = 0;
    *error_rate = 0;
    uint32_t route_count = 0;
    uint64_t total_lat = 0;
    uint32_t total_req = 0;
    uint32_t total_fail = 0;

    for (uint32_t i = 0; i < store->count; i++) {
        const spl_route_metrics_t *m = &store->routes[i];
        if (!m->active || m->node_id != node_id) continue;

        *total_bytes += m->bytes_routed;
        total_lat += m->latency_avg_us;
        total_req += m->requests_total;
        total_fail += m->requests_failed;
        route_count++;
    }

    if (route_count > 0)
        *avg_latency = (uint32_t)(total_lat / route_count);
    if (total_req > 0)
        *error_rate = (total_fail * 1000) / total_req;

    return (int)route_count;
}

/*
 * Find the route with the highest throughput for load balancing.
 */
uint32_t spl_route_metrics_highest_throughput(const spl_route_metrics_store_t *store,
                                              uint32_t span_id) {
    if (!store) return 0;
    uint32_t best = 0;
    uint64_t best_bytes = 0;
    for (uint32_t i = 0; i < store->count; i++) {
        const spl_route_metrics_t *m = &store->routes[i];
        if (!m->active || m->span_id != span_id) continue;
        if (m->bytes_routed > best_bytes) {
            best_bytes = m->bytes_routed;
            best = m->route_id;
        }
    }
    return best;
}

/*
 * Compute weighted score combining latency, throughput and error rate.
 * Lower is better. Used for multi-criteria route selection.
 */
int spl_route_metrics_weighted_score(const spl_route_metrics_t *m,
                                     uint32_t lat_weight,
                                     uint32_t err_weight,
                                     uint32_t throughput_weight) {
    if (!m) return INT32_MAX;

    int score = 0;
    score += (int)(m->latency_avg_us / 100) * (int)lat_weight;
    if (m->requests_total > 0) {
        int err_pct = (int)((m->requests_failed * 100) / m->requests_total);
        score += err_pct * (int)err_weight;
    }
    if (m->bytes_routed > 0) {
        int throughput_inv = (int)(1000000 / (m->bytes_routed / 1024 + 1));
        score += throughput_inv * (int)throughput_weight;
    }
    return score;
}
