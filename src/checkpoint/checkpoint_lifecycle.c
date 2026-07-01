#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Checkpoint lifecycle management: tracks checkpoint creation,
 * expiration, and garbage collection across multiple spans.
 */

typedef struct {
    uint32_t checkpoint_id;
    uint32_t span_id;
    uint32_t epoch_created;
    uint32_t epoch_expires;
    uint64_t data_size_bytes;
    uint32_t segment_snapshot_count;
    spl_ckpt_phase_t phase;
    bool     pinned;   /* prevented from GC */
    bool     expired;
} spl_ckpt_lifecycle_entry_t;

#define SPL_CKPT_LIFECYCLE_MAX 128

typedef struct {
    spl_ckpt_lifecycle_entry_t entries[SPL_CKPT_LIFECYCLE_MAX];
    uint32_t count;
    uint32_t max_age_epochs;
    uint32_t max_count_per_span;
} spl_ckpt_lifecycle_mgr_t;

int spl_ckpt_lifecycle_init(spl_ckpt_lifecycle_mgr_t *mgr,
                            uint32_t max_age, uint32_t max_per_span) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(*mgr));
    mgr->max_age_epochs = max_age > 0 ? max_age : 100;
    mgr->max_count_per_span = max_per_span > 0 ? max_per_span : 5;
    return 0;
}

int spl_ckpt_lifecycle_register(spl_ckpt_lifecycle_mgr_t *mgr,
                                uint32_t ckpt_id, uint32_t span_id,
                                uint32_t current_epoch, uint64_t data_size,
                                uint32_t seg_count) {
    if (!mgr || mgr->count >= SPL_CKPT_LIFECYCLE_MAX) return -1;

    spl_ckpt_lifecycle_entry_t *e = &mgr->entries[mgr->count++];
    e->checkpoint_id = ckpt_id;
    e->span_id = span_id;
    e->epoch_created = current_epoch;
    e->epoch_expires = current_epoch + mgr->max_age_epochs;
    e->data_size_bytes = data_size;
    e->segment_snapshot_count = seg_count;
    e->phase = SPL_CKPT_IN_PROGRESS;
    e->pinned = false;
    e->expired = false;

    return 0;
}

int spl_ckpt_lifecycle_evaluate(spl_ckpt_lifecycle_mgr_t *mgr,
                                uint32_t current_epoch) {
    if (!mgr) return -1;

    int expired = 0;

    for (uint32_t i = 0; i < mgr->count; i++) {
        spl_ckpt_lifecycle_entry_t *e = &mgr->entries[i];
        if (e->expired || e->pinned) continue;

        /* Age-based expiry */
        if (current_epoch > e->epoch_expires) {
            e->expired = true;
            expired++;
            continue;
        }

        /* Count-based eviction: too many checkpoints for this span */
        uint32_t span_count = 0;
        for (uint32_t j = 0; j < mgr->count; j++) {
            if (mgr->entries[j].span_id == e->span_id &&
                !mgr->entries[j].expired)
                span_count++;
        }

        if (span_count > mgr->max_count_per_span) {
            /* Find oldest for this span (not pinned) */
            uint32_t oldest_epoch = UINT32_MAX;
            uint32_t oldest_idx = i;
            for (uint32_t j = 0; j < mgr->count; j++) {
                if (mgr->entries[j].span_id == e->span_id &&
                    !mgr->entries[j].expired && !mgr->entries[j].pinned &&
                    mgr->entries[j].epoch_created < oldest_epoch) {
                    oldest_epoch = mgr->entries[j].epoch_created;
                    oldest_idx = j;
                }
            }
            mgr->entries[oldest_idx].expired = true;
            expired++;
        }
    }

    return expired;
}

int spl_ckpt_lifecycle_pin(spl_ckpt_lifecycle_mgr_t *mgr,
                           uint32_t ckpt_id) {
    if (!mgr) return -1;
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (mgr->entries[i].checkpoint_id == ckpt_id) {
            mgr->entries[i].pinned = true;
            return 0;
        }
    }
    return -1;
}

int spl_ckpt_lifecycle_unpin(spl_ckpt_lifecycle_mgr_t *mgr,
                             uint32_t ckpt_id) {
    if (!mgr) return -1;
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (mgr->entries[i].checkpoint_id == ckpt_id) {
            mgr->entries[i].pinned = false;
            return 0;
        }
    }
    return -1;
}

uint64_t spl_ckpt_lifecycle_total_size(const spl_ckpt_lifecycle_mgr_t *mgr) {
    if (!mgr) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (!mgr->entries[i].expired)
            total += mgr->entries[i].data_size_bytes;
    }
    return total;
}

int spl_ckpt_lifecycle_gc(spl_ckpt_lifecycle_mgr_t *mgr) {
    if (!mgr) return -1;
    /* Compact: remove expired entries */
    uint32_t write = 0;
    for (uint32_t read = 0; read < mgr->count; read++) {
        if (!mgr->entries[read].expired) {
            if (write != read)
                mgr->entries[write] = mgr->entries[read];
            write++;
        }
    }
    int freed = (int)(mgr->count - write);
    mgr->count = write;
    return freed;
}
