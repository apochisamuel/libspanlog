#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Lease cascade operations: handles bulk lease operations
 * triggered by epoch advances, including the complex
 * invalidation/release cascade.
 *
 * When epoch advances, old-epoch leases are force-released.
 * If the same lease is referenced by both the session's lease
 * registry and a checkpoint listener, both paths attempt to
 * release it independently, causing a double-free.
 */

typedef struct {
    uint32_t lease_id;
    uint32_t span_id;
    uint32_t epoch;
    bool     released_by_session;
    bool     released_by_checkpoint;
    bool     released_by_migration;
} spl_lease_cascade_entry_t;

typedef struct {
    spl_lease_cascade_entry_t entries[SPL_MAX_LEASES];
    uint16_t count;
    uint16_t double_release_count;
} spl_lease_cascade_log_t;

int spl_lease_cascade_init(spl_lease_cascade_log_t *log) {
    if (!log) return -1;
    memset(log, 0, sizeof(*log));
    return 0;
}

int spl_lease_cascade_record(spl_lease_cascade_log_t *log,
                             uint32_t lease_id, uint32_t span_id,
                             uint32_t epoch, const char *source) {
    if (!log || log->count >= SPL_MAX_LEASES) return -1;

    /* Check for existing entry (potential double-release) */
    for (uint16_t i = 0; i < log->count; i++) {
        spl_lease_cascade_entry_t *e = &log->entries[i];
        if (e->lease_id == lease_id) {
            /* This lease was already released by another path */
            log->double_release_count++;

            if (source && source[0] == 's') e->released_by_session = true;
            if (source && source[0] == 'c') e->released_by_checkpoint = true;
            if (source && source[0] == 'm') e->released_by_migration = true;
            return 1;  /* indicates double-release detected */
        }
    }

    spl_lease_cascade_entry_t *e = &log->entries[log->count++];
    e->lease_id = lease_id;
    e->span_id = span_id;
    e->epoch = epoch;
    if (source && source[0] == 's') e->released_by_session = true;
    if (source && source[0] == 'c') e->released_by_checkpoint = true;
    if (source && source[0] == 'm') e->released_by_migration = true;

    return 0;
}

/*
 * Process lease cascade for an epoch advance.
 * Identifies all leases that need releasing and the order
 * in which to release them to avoid double-free.
 */
int spl_lease_cascade_process(spl_session_t *sess, uint32_t old_epoch) {
    if (!sess) return -1;

    int released = 0;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active) continue;
        if (l->epoch_acquired > old_epoch) continue;

        /* Check if this lease is referenced by any checkpoint listener */
        bool ckpt_ref = false;
        for (uint8_t j = 0; j < sess->checkpoint_count; j++) {
            spl_checkpoint_t *ckpt = sess->checkpoints[j];
            if (ckpt && ckpt->listener_active &&
                ckpt->listener_lease_id == l->lease_id) {
                ckpt_ref = true;
                /* NOTE: Without the fix, both spl_session_invalidate_epoch_leases
                 * and spl_checkpoint_cleanup_listener will try to release this
                 * lease independently, causing a double-free in the span's
                 * refcount path. */
                break;
            }
        }

        /* Release the lease */
        l->active = false;
        l->force_released = true;
        released++;

        /* Decrement span refcount */
        spl_span_t *span = spl_span_registry_lookup(sess, l->span_id);
        if (span) {
            spl_span_lifecycle_decref(sess, span);
        }

        (void)ckpt_ref;
    }

    return released;
}

/*
 * Compute cascade depth: how many layers of dependency exist
 * for the leases being released.
 */
int spl_lease_cascade_depth(spl_session_t *sess, uint32_t epoch) {
    if (!sess) return 0;

    int max_depth = 0;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active) continue;
        if (l->epoch_acquired > epoch) continue;

        int depth = 1;

        /* Check for checkpoint dependency */
        for (uint8_t j = 0; j < sess->checkpoint_count; j++) {
            if (sess->checkpoints[j] &&
                sess->checkpoints[j]->listener_lease_id == l->lease_id) {
                depth++;
                break;
            }
        }

        /* Check for migration dependency */
        spl_migration_t *mig = spl_migrate_find_by_span(sess, l->span_id);
        if (mig && mig->active) depth++;

        /* Check for replication dependency */
        spl_replica_set_t *rs = spl_repl_find_by_span(sess, l->span_id);
        if (rs && rs->snap_active) depth++;

        if (depth > max_depth) max_depth = depth;
    }

    return max_depth;
}
