#include "../common/types.h"
#include "../common/alloc.h"

int spl_retention_gc_eligible(spl_session_t *sess, spl_span_t *span,
                              spl_segment_t **out_eligible, uint16_t *count) {
    if (!sess || !span || !out_eligible || !count)
        return SPL_ERR_INVALID_STATE;

    *count = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        spl_segment_t *seg = span->segments[i];
        if (!seg || seg->state != SPL_LIFECYCLE_ACTIVE) continue;
        if (seg->compacted) {
            out_eligible[*count] = seg;
            (*count)++;
        }
    }

    return SPL_OK;
}

int spl_retention_gc_collect(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Collect eligible segments.
     * does not check for pending stream flushes. */
    spl_segment_t *eligible[SPL_MAX_SEGMENTS];
    uint16_t count = 0;
    spl_retention_gc_eligible(sess, span, eligible, &count);

    for (uint16_t i = 0; i < count; i++) {
        spl_segment_gc_reclaim(sess, eligible[i]);
    }

    return SPL_OK;
}

int spl_retention_handle_collect(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    return spl_retention_gc_collect(sess, span_id);
}
