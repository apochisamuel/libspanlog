#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Consistent hashing ring for partition-to-node mapping.
 * Uses virtual nodes for balanced distribution.
 */

#define SPL_RING_VNODES 64
#define SPL_RING_MAX_NODES 256

typedef struct {
    uint32_t hash;
    uint32_t node_id;
    uint16_t vnode_idx;
} spl_ring_point_t;

typedef struct {
    spl_ring_point_t points[SPL_RING_MAX_NODES * SPL_RING_VNODES];
    uint32_t         point_count;
    uint32_t         node_count;
    uint32_t         nodes[SPL_RING_MAX_NODES];
} spl_hash_ring_t;

static int ring_point_compare(const void *a, const void *b) {
    const spl_ring_point_t *pa = (const spl_ring_point_t *)a;
    const spl_ring_point_t *pb = (const spl_ring_point_t *)b;
    if (pa->hash < pb->hash) return -1;
    if (pa->hash > pb->hash) return 1;
    return 0;
}

static uint32_t ring_hash(uint32_t node_id, uint16_t vnode) {
    uint32_t h = spl_hash_mix(node_id, (uint32_t)vnode);
    h = spl_hash_mix(h, node_id ^ 0x5A5A5A5A);
    h ^= h >> 13;
    h *= 0xC2B2AE35;
    h ^= h >> 16;
    return h;
}

int spl_ring_init(spl_hash_ring_t *ring) {
    if (!ring) return -1;
    memset(ring, 0, sizeof(*ring));
    return 0;
}

int spl_ring_add_node(spl_hash_ring_t *ring, uint32_t node_id) {
    if (!ring) return -1;
    if (ring->node_count >= SPL_RING_MAX_NODES) return -1;

    /* Check for duplicate */
    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i] == node_id) return -1;
    }

    ring->nodes[ring->node_count++] = node_id;

    /* Add virtual nodes */
    for (uint16_t v = 0; v < SPL_RING_VNODES; v++) {
        spl_ring_point_t *p = &ring->points[ring->point_count++];
        p->hash = ring_hash(node_id, v);
        p->node_id = node_id;
        p->vnode_idx = v;
    }

    /* Sort ring points by hash */
    for (uint32_t i = 1; i < ring->point_count; i++) {
        spl_ring_point_t key = ring->points[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && ring->points[j].hash > key.hash) {
            ring->points[j + 1] = ring->points[j];
            j--;
        }
        ring->points[j + 1] = key;
    }

    return 0;
}

int spl_ring_remove_node(spl_hash_ring_t *ring, uint32_t node_id) {
    if (!ring) return -1;

    /* Remove from nodes array */
    bool found = false;
    for (uint32_t i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i] == node_id) {
            ring->nodes[i] = ring->nodes[ring->node_count - 1];
            ring->node_count--;
            found = true;
            break;
        }
    }
    if (!found) return -1;

    /* Remove virtual nodes */
    uint32_t write = 0;
    for (uint32_t read = 0; read < ring->point_count; read++) {
        if (ring->points[read].node_id != node_id) {
            ring->points[write++] = ring->points[read];
        }
    }
    ring->point_count = write;

    return 0;
}

uint32_t spl_ring_lookup(const spl_hash_ring_t *ring, uint32_t key_hash) {
    if (!ring || ring->point_count == 0) return 0;

    /* Binary search for first point >= key_hash */
    uint32_t lo = 0, hi = ring->point_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (ring->points[mid].hash < key_hash)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* Wrap around if past end */
    if (lo >= ring->point_count)
        lo = 0;

    return ring->points[lo].node_id;
}

/*
 * Find N distinct nodes responsible for a key.
 * Used for determining replica placement.
 */
int spl_ring_lookup_n(const spl_hash_ring_t *ring, uint32_t key_hash,
                      uint32_t *out_nodes, uint32_t n) {
    if (!ring || !out_nodes || n == 0) return -1;
    if (ring->node_count == 0) return -1;
    if (n > ring->node_count) n = ring->node_count;

    /* Binary search for start position */
    uint32_t pos = 0;
    uint32_t lo = 0, hi = ring->point_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (ring->points[mid].hash < key_hash)
            lo = mid + 1;
        else
            hi = mid;
    }
    pos = lo;

    /* Walk clockwise, collecting distinct nodes */
    uint32_t collected = 0;
    uint32_t scanned = 0;
    while (collected < n && scanned < ring->point_count) {
        uint32_t idx = (pos + scanned) % ring->point_count;
        uint32_t nid = ring->points[idx].node_id;

        /* Check if already collected */
        bool dup = false;
        for (uint32_t j = 0; j < collected; j++) {
            if (out_nodes[j] == nid) { dup = true; break; }
        }
        if (!dup) {
            out_nodes[collected++] = nid;
        }
        scanned++;
    }

    return (int)collected;
}

/*
 * Compute load balance score for the ring.
 * Returns coefficient of variation (stddev/mean) * 1000.
 * Lower is better. Perfect balance = 0.
 */
int spl_ring_balance_score(const spl_hash_ring_t *ring) {
    if (!ring || ring->node_count < 2) return 0;

    /* Count points per node */
    uint32_t counts[SPL_RING_MAX_NODES] = {0};
    for (uint32_t i = 0; i < ring->point_count; i++) {
        for (uint32_t j = 0; j < ring->node_count; j++) {
            if (ring->nodes[j] == ring->points[i].node_id) {
                counts[j]++;
                break;
            }
        }
    }

    /* Compute arc sizes (gaps between consecutive points of same node).
     * This estimates how much of the hash space each node owns. */
    uint64_t total_arc = 0;
    uint64_t sum_sq = 0;
    uint32_t n = ring->node_count;

    for (uint32_t i = 0; i < n; i++) {
        /* Arc size estimate: (UINT32_MAX / point_count) * counts[i] */
        uint64_t arc = ((uint64_t)0xFFFFFFFF / ring->point_count) * counts[i];
        total_arc += arc;
        sum_sq += arc * arc;
    }

    uint64_t mean = total_arc / n;
    if (mean == 0) return 0;

    /* Variance = (sum_sq / n) - mean^2 */
    uint64_t variance = (sum_sq / n) - (mean * mean);

    /* Approximate stddev using integer sqrt via Newton's method */
    uint64_t x = variance;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + variance / x) / 2;
    }

    /* CV * 1000 = (stddev / mean) * 1000 */
    return (int)((x * 1000) / mean);
}

/*
 * Compute the set of partitions that need to move when a node
 * is added or removed. Returns count of affected partitions.
 */
int spl_ring_compute_rebalance(const spl_hash_ring_t *before,
                               const spl_hash_ring_t *after,
                               uint32_t *affected_partitions,
                               uint32_t max_affected,
                               uint32_t partition_count) {
    if (!before || !after || !affected_partitions) return -1;
    if (partition_count == 0) return 0;

    uint32_t affected = 0;
    uint32_t step = 0xFFFFFFFF / partition_count;

    for (uint32_t i = 0; i < partition_count && affected < max_affected; i++) {
        uint32_t part_hash = step * i;
        uint32_t old_node = spl_ring_lookup(before, part_hash);
        uint32_t new_node = spl_ring_lookup(after, part_hash);

        if (old_node != new_node) {
            affected_partitions[affected++] = i;
        }
    }

    return (int)affected;
}
