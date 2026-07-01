#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Checkpoint diff computation: determines which segments changed
 * between two checkpoints to enable incremental snapshots.
 * Uses segment generation numbers and content hashes.
 */

typedef struct {
    uint32_t segment_id;
    uint32_t old_generation;
    uint32_t new_generation;
    uint32_t old_hash;
    uint32_t new_hash;
    size_t   old_size;
    size_t   new_size;
    bool     modified;
    bool     added;
    bool     removed;
} spl_ckpt_diff_entry_t;

typedef struct {
    spl_ckpt_diff_entry_t *entries;
    uint16_t               count;
    uint16_t               capacity;
    uint64_t               total_changed_bytes;
    uint64_t               total_unchanged_bytes;
} spl_ckpt_diff_t;

spl_ckpt_diff_t *spl_ckpt_diff_create(uint16_t capacity) {
    spl_ckpt_diff_t *diff = spl_calloc(1, sizeof(spl_ckpt_diff_t));
    if (!diff) return NULL;

    diff->entries = spl_calloc(capacity, sizeof(spl_ckpt_diff_entry_t));
    if (!diff->entries) { spl_free(diff); return NULL; }

    diff->capacity = capacity;
    diff->count = 0;
    return diff;
}

void spl_ckpt_diff_destroy(spl_ckpt_diff_t *diff) {
    if (!diff) return;
    spl_free(diff->entries);
    spl_free(diff);
}

int spl_ckpt_diff_compute(spl_ckpt_diff_t *diff,
                          spl_checkpoint_t *old_ckpt,
                          spl_checkpoint_t *new_ckpt) {
    if (!diff || !old_ckpt || !new_ckpt) return -1;

    diff->count = 0;
    diff->total_changed_bytes = 0;
    diff->total_unchanged_bytes = 0;

    /* Compare each segment in new checkpoint against old */
    for (uint16_t i = 0; i < new_ckpt->snap_segment_count; i++) {
        spl_segment_t *new_seg = new_ckpt->snap_segments[i];
        if (!new_seg) continue;
        if (diff->count >= diff->capacity) break;

        spl_ckpt_diff_entry_t *entry = &diff->entries[diff->count];
        entry->segment_id = new_seg->segment_id;
        entry->new_generation = new_seg->generation;
        entry->new_hash = spl_crc32c(new_seg->data, new_seg->used);
        entry->new_size = new_seg->used;

        /* Find corresponding segment in old checkpoint */
        bool found = false;
        for (uint16_t j = 0; j < old_ckpt->snap_segment_count; j++) {
            spl_segment_t *old_seg = old_ckpt->snap_segments[j];
            if (!old_seg) continue;
            if (old_seg->segment_id == new_seg->segment_id) {
                entry->old_generation = old_seg->generation;
                entry->old_hash = spl_crc32c(old_seg->data, old_seg->used);
                entry->old_size = old_seg->used;
                found = true;

                if (entry->old_hash != entry->new_hash ||
                    entry->old_generation != entry->new_generation) {
                    entry->modified = true;
                    diff->total_changed_bytes += new_seg->used;
                } else {
                    entry->modified = false;
                    diff->total_unchanged_bytes += new_seg->used;
                }
                break;
            }
        }

        if (!found) {
            entry->added = true;
            entry->old_generation = 0;
            entry->old_hash = 0;
            entry->old_size = 0;
            diff->total_changed_bytes += new_seg->used;
        }

        diff->count++;
    }

    /* Check for removed segments */
    for (uint16_t j = 0; j < old_ckpt->snap_segment_count; j++) {
        spl_segment_t *old_seg = old_ckpt->snap_segments[j];
        if (!old_seg) continue;

        bool still_exists = false;
        for (uint16_t i = 0; i < new_ckpt->snap_segment_count; i++) {
            if (new_ckpt->snap_segments[i] &&
                new_ckpt->snap_segments[i]->segment_id == old_seg->segment_id) {
                still_exists = true;
                break;
            }
        }

        if (!still_exists && diff->count < diff->capacity) {
            spl_ckpt_diff_entry_t *entry = &diff->entries[diff->count++];
            entry->segment_id = old_seg->segment_id;
            entry->removed = true;
            entry->old_size = old_seg->used;
            diff->total_changed_bytes += old_seg->used;
        }
    }

    return (int)diff->count;
}

/*
 * Compute estimated transfer size for an incremental checkpoint.
 * Returns bytes that need to be transferred.
 */
uint64_t spl_ckpt_diff_transfer_size(const spl_ckpt_diff_t *diff) {
    if (!diff) return 0;

    uint64_t total = 0;
    for (uint16_t i = 0; i < diff->count; i++) {
        const spl_ckpt_diff_entry_t *e = &diff->entries[i];
        if (e->modified || e->added)
            total += e->new_size;
    }

    /* Add overhead: 32 bytes per entry for metadata */
    total += (uint64_t)diff->count * 32;
    return total;
}

/*
 * Compute savings ratio: 0-100, where 100 = no data needs transfer.
 */
int spl_ckpt_diff_savings_ratio(const spl_ckpt_diff_t *diff) {
    if (!diff) return 0;
    uint64_t total = diff->total_changed_bytes + diff->total_unchanged_bytes;
    if (total == 0) return 100;
    return (int)((diff->total_unchanged_bytes * 100) / total);
}
