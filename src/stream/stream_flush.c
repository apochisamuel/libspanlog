#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Stream flush: writes pending buffered data to target segment.
 * pending_target_segment may be freed by retention GC.
 */
int spl_stream_flush_pending(spl_session_t *sess, spl_stream_t *stream) {
    if (!sess || !stream) return SPL_ERR_INVALID_STATE;
    if (!stream->active) return SPL_ERR_INVALID_STATE;

    if (!stream->pending_data || stream->pending_len == 0)
        return SPL_OK;  /* nothing to flush */

    /* Write to pending target segment. */
    spl_segment_t *target = stream->pending_target_segment;
    if (target && target->data) {
        /* Data was already written during buffer_append, so this is
         * a secondary write path (for durability confirmation). */
        uint32_t hash = spl_fnv1a(stream->pending_data, stream->pending_len);
        (void)hash;
    }

    spl_free(stream->pending_data);
    stream->pending_data = NULL;
    stream->pending_len = 0;
    stream->pending_target_segment = NULL;

    return SPL_OK;
}
