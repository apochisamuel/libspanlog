#include "../common/types.h"
#include "../common/alloc.h"

int spl_handle_drain_deferred(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    /* Process deferred span deletions */
    if (sess->deferred_delete_grace > 0) {
        sess->deferred_delete_grace--;
        return SPL_OK;
    }

    for (uint16_t i = 0; i < sess->deferred_delete_count; i++) {
        spl_span_t *span = sess->deferred_delete_list[i];
        if (!span) continue;
        if (span->state == SPL_LIFECYCLE_DEFERRED) {
            /* Invalidate cursors on span close */
            spl_cursor_invalidate_for_span(sess, span->span_id);
            spl_span_registry_destroy(sess, span);
            sess->deferred_delete_list[i] = NULL;
        }
    }

    /* Compact deferred list */
    uint16_t w = 0;
    for (uint16_t r = 0; r < sess->deferred_delete_count; r++) {
        if (sess->deferred_delete_list[r])
            sess->deferred_delete_list[w++] = sess->deferred_delete_list[r];
    }
    sess->deferred_delete_count = w;

    return SPL_OK;
}

int spl_handle_gc_trigger(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    /* GC drained channels */
    spl_session_gc_drained_channels(sess);

    /* GC freed segments from session registry */
    uint16_t w = 0;
    for (uint16_t r = 0; r < sess->segment_count; r++) {
        if (sess->segments[r] && sess->segments[r]->state != SPL_LIFECYCLE_FREED) {
            sess->segments[w++] = sess->segments[r];
        }
    }
    for (uint16_t i = w; i < sess->segment_count; i++)
        sess->segments[i] = NULL;
    sess->segment_count = w;

    return SPL_OK;
}
