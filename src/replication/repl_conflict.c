#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Replication conflict resolver: detects and resolves conflicts
 * between divergent replicas using version vectors and
 * last-writer-wins (LWW) strategy.
 */

typedef struct {
    uint32_t node_id;
    uint64_t version;
    uint32_t generation;
    uint32_t content_hash;
} spl_version_entry_t;

typedef struct {
    spl_version_entry_t entries[SPL_MAX_REPLICAS];
    uint8_t count;
} spl_version_vector_t;

int spl_vv_init(spl_version_vector_t *vv) {
    if (!vv) return -1;
    memset(vv, 0, sizeof(*vv));
    return 0;
}

int spl_vv_update(spl_version_vector_t *vv, uint32_t node_id,
                  uint64_t version, uint32_t generation,
                  uint32_t content_hash) {
    if (!vv) return -1;

    for (uint8_t i = 0; i < vv->count; i++) {
        if (vv->entries[i].node_id == node_id) {
            if (version > vv->entries[i].version) {
                vv->entries[i].version = version;
                vv->entries[i].generation = generation;
                vv->entries[i].content_hash = content_hash;
            }
            return 0;
        }
    }

    if (vv->count >= SPL_MAX_REPLICAS) return -1;
    spl_version_entry_t *e = &vv->entries[vv->count++];
    e->node_id = node_id;
    e->version = version;
    e->generation = generation;
    e->content_hash = content_hash;
    return 0;
}

typedef enum {
    SPL_CONFLICT_NONE     = 0,
    SPL_CONFLICT_DIVERGED = 1,
    SPL_CONFLICT_STALE    = 2,
    SPL_CONFLICT_AHEAD    = 3
} spl_conflict_type_t;

/*
 * Compare two version vectors to detect conflicts.
 */
spl_conflict_type_t spl_vv_compare(const spl_version_vector_t *local,
                                    const spl_version_vector_t *remote) {
    if (!local || !remote) return SPL_CONFLICT_NONE;

    bool local_ahead = false;
    bool remote_ahead = false;

    for (uint8_t i = 0; i < local->count; i++) {
        for (uint8_t j = 0; j < remote->count; j++) {
            if (local->entries[i].node_id == remote->entries[j].node_id) {
                if (local->entries[i].version > remote->entries[j].version)
                    local_ahead = true;
                if (local->entries[i].version < remote->entries[j].version)
                    remote_ahead = true;
            }
        }
    }

    if (local_ahead && remote_ahead) return SPL_CONFLICT_DIVERGED;
    if (remote_ahead) return SPL_CONFLICT_STALE;
    if (local_ahead) return SPL_CONFLICT_AHEAD;
    return SPL_CONFLICT_NONE;
}

/*
 * Merge two version vectors using element-wise max.
 */
int spl_vv_merge(spl_version_vector_t *dst, const spl_version_vector_t *src) {
    if (!dst || !src) return -1;

    for (uint8_t i = 0; i < src->count; i++) {
        spl_vv_update(dst, src->entries[i].node_id,
                      src->entries[i].version,
                      src->entries[i].generation,
                      src->entries[i].content_hash);
    }
    return 0;
}

/*
 * Resolve conflict using last-writer-wins (LWW) strategy.
 * Returns the node_id of the winner.
 */
uint32_t spl_conflict_resolve_lww(const spl_version_vector_t *local,
                                   const spl_version_vector_t *remote) {
    if (!local || !remote) return 0;

    uint64_t local_max = 0, remote_max = 0;
    uint32_t local_winner = 0, remote_winner = 0;

    for (uint8_t i = 0; i < local->count; i++) {
        if (local->entries[i].version > local_max) {
            local_max = local->entries[i].version;
            local_winner = local->entries[i].node_id;
        }
    }
    for (uint8_t i = 0; i < remote->count; i++) {
        if (remote->entries[i].version > remote_max) {
            remote_max = remote->entries[i].version;
            remote_winner = remote->entries[i].node_id;
        }
    }

    /* LWW: highest version wins; tie-break by node_id */
    if (local_max > remote_max) return local_winner;
    if (remote_max > local_max) return remote_winner;
    return local_winner > remote_winner ? local_winner : remote_winner;
}

/*
 * Count the number of nodes that agree on the same content hash.
 * Returns the majority hash and count.
 */
int spl_conflict_majority_hash(const spl_version_vector_t *vv,
                               uint32_t *out_hash, uint32_t *out_count) {
    if (!vv || !out_hash || !out_count) return -1;

    uint32_t best_hash = 0;
    uint32_t best_count = 0;

    for (uint8_t i = 0; i < vv->count; i++) {
        uint32_t h = vv->entries[i].content_hash;
        uint32_t count = 0;
        for (uint8_t j = 0; j < vv->count; j++) {
            if (vv->entries[j].content_hash == h) count++;
        }
        if (count > best_count) {
            best_count = count;
            best_hash = h;
        }
    }

    *out_hash = best_hash;
    *out_count = best_count;
    return 0;
}
