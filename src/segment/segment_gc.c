#include "../common/types.h"
#include "../common/alloc.h"

int spl_segment_gc_reclaim(spl_session_t *sess, spl_segment_t *seg) {
    if (!sess || !seg) return SPL_ERR_INVALID_STATE;

    /* Free segment data */
    spl_free(seg->data);
    seg->data = NULL;
    seg->state = SPL_LIFECYCLE_FREED;

    /* Remove from session */
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] == seg) {
            spl_free(seg);
            sess->segments[i] = NULL;
            break;
        }
    }

    return SPL_OK;
}

int spl_segment_gc_span(spl_session_t *sess, spl_span_t *span) {
    if (!sess || !span) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i]) {
            spl_segment_free(sess, span->segments[i]);
            /* Also remove from session and free struct */
            for (uint16_t j = 0; j < sess->segment_count; j++) {
                if (sess->segments[j] == span->segments[i]) {
                    spl_free(span->segments[i]);
                    sess->segments[j] = NULL;
                    break;
                }
            }
            span->segments[i] = NULL;
        }
    }
    span->segment_count = 0;

    return SPL_OK;
}
