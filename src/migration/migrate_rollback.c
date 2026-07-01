#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration rollback: handles failure recovery during migration.
 * Restores source span state if migration fails at any phase.
 */

typedef struct {
    uint32_t migration_id;
    uint32_t span_id;
    spl_migrate_phase_t phase_at_failure;
    uint32_t segment_count_snapshot;
    uint64_t total_bytes_snapshot;
    uint32_t generation_snapshot;
    spl_lifecycle_state_t state_snapshot;
} spl_migrate_rollback_t;

int spl_migrate_snapshot_for_rollback(spl_session_t *sess,
                                     spl_migration_t *mig,
                                     spl_migrate_rollback_t *rb) {
    if (!sess || !mig || !rb) return -1;

    spl_span_t *span = spl_span_registry_lookup(sess, mig->span_id);
    if (!span) return -1;

    rb->migration_id = mig->migration_id;
    rb->span_id = mig->span_id;
    rb->phase_at_failure = mig->phase;
    rb->segment_count_snapshot = span->segment_count;
    rb->total_bytes_snapshot = span->total_bytes;
    rb->generation_snapshot = span->generation;
    rb->state_snapshot = span->state;

    return 0;
}

int spl_migrate_rollback(spl_session_t *sess,
                         const spl_migrate_rollback_t *rb) {
    if (!sess || !rb) return -1;

    spl_span_t *span = spl_span_registry_lookup(sess, rb->span_id);
    if (!span) return -1;

    /* Restore span state */
    span->generation = rb->generation_snapshot;
    span->state = rb->state_snapshot;
    span->migration_source = false;
    span->migration_id = 0;

    /* Cancel the migration */
    spl_migration_t *mig = spl_migrate_lookup(sess, rb->migration_id);
    if (mig) {
        mig->phase = SPL_MIGRATE_ABORTED;
        mig->active = false;
    }

    /* If segments were reallocated during prepare, they can't be
     * un-reallocated. The data is intact (copied during realloc)
     * but the old buffers are freed. Cursors must be revalidated. */
    spl_cursor_invalidate_for_span(sess, rb->span_id);

    return 0;
}

/*
 * Compute rollback cost: estimates how much work is lost on rollback.
 * Returns bytes that were transferred but will be discarded.
 */
uint64_t spl_migrate_rollback_cost(const spl_migration_t *mig) {
    if (!mig) return 0;
    return mig->bytes_transferred;
}

/*
 * Check if rollback is safe: validates that source data is still
 * intact and can be reverted to pre-migration state.
 */
int spl_migrate_rollback_safe(spl_session_t *sess,
                              const spl_migration_t *mig) {
    if (!sess || !mig) return -1;

    spl_span_t *span = spl_span_registry_lookup(sess, mig->span_id);
    if (!span) return -1;  /* span destroyed, can't rollback */

    /* Check segments still exist */
    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (!span->segments[i] || span->segments[i]->state == SPL_LIFECYCLE_FREED)
            return -1;  /* segment freed, partial rollback at best */
    }

    /* Check no concurrent migrations */
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] &&
            sess->migrations[i] != mig &&
            sess->migrations[i]->span_id == mig->span_id &&
            sess->migrations[i]->active) {
            return -1;  /* conflicting migration */
        }
    }

    return 0;
}

/*
 * Estimate time to complete migration vs time to rollback.
 * Returns positive if continuing is faster, negative if rollback is faster.
 */
int spl_migrate_continue_vs_rollback(const spl_migration_t *mig,
                                     uint32_t estimated_bw_kbps) {
    if (!mig || estimated_bw_kbps == 0) return -1;

    uint64_t remaining = mig->total_bytes - mig->bytes_transferred;
    uint64_t rollback_bytes = mig->bytes_transferred;

    /* Time to continue (ms) */
    uint64_t continue_ms = (remaining * 8) / estimated_bw_kbps;
    /* Time to rollback (ms) — typically faster as it's just metadata */
    uint64_t rollback_ms = (rollback_bytes * 8) / (estimated_bw_kbps * 4);

    return (int)((int64_t)rollback_ms - (int64_t)continue_ms);
}
