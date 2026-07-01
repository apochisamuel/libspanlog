#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Partition allocator: manages partition ID assignment and tracks
 * partition-to-node mappings with load-aware placement.
 */

#define SPL_ALLOC_MAX_PARTITIONS 256

typedef struct {
    uint32_t partition_id;
    uint32_t node_id;
    uint64_t bytes_total;
    uint32_t span_count;
    bool     active;
} spl_partition_alloc_entry_t;

typedef struct {
    spl_partition_alloc_entry_t entries[SPL_ALLOC_MAX_PARTITIONS];
    uint32_t count;
    uint32_t next_partition_id;
    uint32_t target_per_node;
} spl_partition_allocator_t;

int spl_palloc_init(spl_partition_allocator_t *alloc, uint32_t target_per_node) {
    if (!alloc) return -1;
    memset(alloc, 0, sizeof(*alloc));
    alloc->next_partition_id = 1;
    alloc->target_per_node = target_per_node > 0 ? target_per_node : 16;
    return 0;
}

/*
 * Find the least-loaded node for a new partition.
 */
uint32_t spl_palloc_best_node(const spl_partition_allocator_t *alloc,
                              const uint32_t *node_ids, uint32_t node_count) {
    if (!alloc || !node_ids || node_count == 0) return 0;

    uint32_t best_node = node_ids[0];
    uint32_t min_load = UINT32_MAX;

    for (uint32_t n = 0; n < node_count; n++) {
        uint32_t load = 0;
        for (uint32_t i = 0; i < alloc->count; i++) {
            if (alloc->entries[i].active &&
                alloc->entries[i].node_id == node_ids[n])
                load++;
        }
        if (load < min_load) {
            min_load = load;
            best_node = node_ids[n];
        }
    }
    return best_node;
}

uint32_t spl_palloc_create(spl_partition_allocator_t *alloc,
                           uint32_t node_id) {
    if (!alloc || alloc->count >= SPL_ALLOC_MAX_PARTITIONS) return 0;

    spl_partition_alloc_entry_t *e = &alloc->entries[alloc->count++];
    e->partition_id = alloc->next_partition_id++;
    e->node_id = node_id;
    e->active = true;
    return e->partition_id;
}

int spl_palloc_move(spl_partition_allocator_t *alloc, uint32_t partition_id,
                    uint32_t new_node_id) {
    if (!alloc) return -1;
    for (uint32_t i = 0; i < alloc->count; i++) {
        if (alloc->entries[i].partition_id == partition_id &&
            alloc->entries[i].active) {
            alloc->entries[i].node_id = new_node_id;
            return 0;
        }
    }
    return -1;
}

int spl_palloc_remove(spl_partition_allocator_t *alloc, uint32_t partition_id) {
    if (!alloc) return -1;
    for (uint32_t i = 0; i < alloc->count; i++) {
        if (alloc->entries[i].partition_id == partition_id) {
            alloc->entries[i].active = false;
            return 0;
        }
    }
    return -1;
}

int spl_palloc_count_for_node(const spl_partition_allocator_t *alloc,
                              uint32_t node_id) {
    if (!alloc) return 0;
    int count = 0;
    for (uint32_t i = 0; i < alloc->count; i++) {
        if (alloc->entries[i].active && alloc->entries[i].node_id == node_id)
            count++;
    }
    return count;
}

/*
 * Auto-rebalance: generate moves to reach target_per_node balance.
 */
int spl_palloc_auto_rebalance(spl_partition_allocator_t *alloc,
                              const uint32_t *node_ids, uint32_t node_count,
                              uint32_t *src_parts, uint32_t *dst_nodes,
                              uint32_t max_moves) {
    if (!alloc || !node_ids || !src_parts || !dst_nodes) return -1;

    uint32_t moves = 0;
    for (uint32_t iter = 0; iter < 100 && moves < max_moves; iter++) {
        /* Find most overloaded and most underloaded nodes */
        uint32_t over_node = 0, under_node = 0;
        int max_excess = 0, max_deficit = 0;

        for (uint32_t n = 0; n < node_count; n++) {
            int count = spl_palloc_count_for_node(alloc, node_ids[n]);
            int diff = count - (int)alloc->target_per_node;
            if (diff > max_excess) { max_excess = diff; over_node = node_ids[n]; }
            if (diff < max_deficit) { max_deficit = diff; under_node = node_ids[n]; }
        }

        if (max_excess <= 0 || max_deficit >= 0) break;

        /* Find a partition on over_node to move */
        for (uint32_t i = 0; i < alloc->count; i++) {
            if (alloc->entries[i].active &&
                alloc->entries[i].node_id == over_node) {
                src_parts[moves] = alloc->entries[i].partition_id;
                dst_nodes[moves] = under_node;
                alloc->entries[i].node_id = under_node;
                moves++;
                break;
            }
        }
    }
    return (int)moves;
}
