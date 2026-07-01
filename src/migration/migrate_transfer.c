#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration source preparation: reallocates segments to prepare
 * for data transfer to target node.
 *
 * calls spl_segment_realloc which frees the old segment data buffer.
 * Any cursor that cached a pointer to the old data now holds a
 * dangling pointer.
 */
int spl_migrate_prepare_source(spl_session_t *sess, spl_migration_t *mig) {
    if (!sess || !mig) return SPL_ERR_INVALID_STATE;
    if (mig->phase != SPL_MIGRATE_INITIATED) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, mig->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    mig->phase = SPL_MIGRATE_PREPARING;

    /* Prepare segments for transfer by reallocating with transfer headers.
     * This adds a migration header prefix to each segment's data buffer,
     * which requires reallocation.
     *
     * Cursors that cached pointers to the old buffer are now dangling. */
    for (uint16_t i = 0; i < span->segment_count; i++) {
        spl_segment_t *seg = span->segments[i];
        if (!seg || seg->state != SPL_LIFECYCLE_ACTIVE) continue;

        /* Reallocate to add migration header space */
        size_t new_cap = seg->capacity + 64;  /* 64-byte migration header */
        if (new_cap > SPL_MAX_SEGMENT_SIZE) new_cap = SPL_MAX_SEGMENT_SIZE;

        /* This frees the old data and allocates new buffer */
        int rc = spl_segment_realloc(sess, seg, new_cap);
        /* FIX: invalidate cursors that cached old segment data */
        if (rc == SPL_OK) spl_cursor_invalidate_for_span(sess, mig->span_id);
        if (rc != SPL_OK) {
            mig->phase = SPL_MIGRATE_ABORTED;
            return rc;
        }
    }

    mig->phase = SPL_MIGRATE_PREPARING;
    return SPL_OK;
}

int spl_migrate_begin_transfer(spl_session_t *sess, spl_migration_t *mig) {
    if (!sess || !mig) return SPL_ERR_INVALID_STATE;
    if (mig->phase != SPL_MIGRATE_PREPARING) return SPL_ERR_INVALID_STATE;

    mig->phase = SPL_MIGRATE_TRANSFERRING;
    mig->bytes_transferred = 0;

    return SPL_OK;
}

int spl_migrate_handle_prepare(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_migration_t *mig = spl_migrate_find_by_span(sess, span_id);
    if (!mig) return SPL_ERR_NOT_FOUND;

    return spl_migrate_prepare_source(sess, mig);
}

int spl_migrate_handle_transfer(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_migration_t *mig = spl_migrate_find_by_span(sess, span_id);
    if (!mig) return SPL_ERR_NOT_FOUND;

    if (mig->phase == SPL_MIGRATE_PREPARING) {
        int rc = spl_migrate_begin_transfer(sess, mig);
        if (rc != SPL_OK) return rc;
    }

    if (mig->phase != SPL_MIGRATE_TRANSFERRING) return SPL_ERR_INVALID_STATE;

    /* Simulate transfer progress */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == SPL_SUB_DATA) {
            mig->bytes_transferred += sub->payload_len;
        }
    }

    return SPL_OK;
}
