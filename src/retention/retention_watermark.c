#include "../common/types.h"

int spl_retention_evaluate_watermark(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Find retention state */
    spl_retention_state_t *ret = NULL;
    for (uint16_t i = 0; i < SPL_MAX_SPANS; i++) {
        if (sess->retention[i].active && sess->retention[i].span_id == span_id) {
            ret = &sess->retention[i];
            break;
        }
    }

    if (!ret) {
        /* Auto-initialize retention with default threshold */
        ret = NULL;
        for (uint16_t i = 0; i < SPL_MAX_SPANS; i++) {
            if (!sess->retention[i].active) {
                sess->retention[i].span_id = span_id;
                sess->retention[i].gc_threshold_bytes = 8192;
                sess->retention[i].watermark_position = 0;
                sess->retention[i].active = true;
                ret = &sess->retention[i];
                break;
            }
        }
        if (!ret) return SPL_ERR_FULL;
    }

    /* Update watermark based on span total bytes */
    ret->watermark_position = span->total_bytes;

    /* Check if GC is needed */
    if (span->total_bytes > ret->gc_threshold_bytes && span->segment_count >= 2) {
        /* Trigger compaction.
         * does not check for in-flight replication snapshots. */
        spl_segment_compact(sess, span);
    }

    return SPL_OK;
}

int spl_retention_handle_eval(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    return spl_retention_evaluate_watermark(sess, span_id);
}
