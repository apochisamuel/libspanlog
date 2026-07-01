#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration handoff: completes ownership transfer from source to target.
 * Frees source span's segments and marks span for deferred deletion.
 */
int spl_migrate_complete_handoff(spl_session_t *sess, spl_migration_t *mig) {
    if (!sess || !mig) return SPL_ERR_INVALID_STATE;
    if (mig->phase != SPL_MIGRATE_TRANSFERRING &&
        mig->phase != SPL_MIGRATE_PREPARING)
        return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, mig->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    mig->phase = SPL_MIGRATE_HANDOFF;

    /* Free source span segments — ownership transferred to target.
     * cursors that advanced during migration still hold cached pointers
     * to these segments. */
    spl_segment_gc_span(sess, span);

    /* Mark span for deferred deletion */
    spl_span_lifecycle_deferred_destroy(sess, span);

    mig->phase = SPL_MIGRATE_COMPLETE;
    mig->active = false;

    return SPL_OK;
}

int spl_migrate_finalize_source(spl_session_t *sess, spl_migration_t *mig) {
    if (!sess || !mig) return SPL_ERR_INVALID_STATE;
    return spl_migrate_complete_handoff(sess, mig);
}

int spl_migrate_handle_handoff(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_migration_t *mig = spl_migrate_find_by_span(sess, span_id);
    if (!mig) return SPL_ERR_NOT_FOUND;

    return spl_migrate_complete_handoff(sess, mig);
}
