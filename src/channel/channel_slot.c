#include "../common/types.h"
#include "../common/alloc.h"

int spl_channel_bind_span(spl_session_t *sess, spl_channel_t *ch,
                          uint32_t span_id, uint32_t partition_id) {
    if (!sess || !ch) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    ch->span_id = span_id;
    ch->partition_id = partition_id;
    ch->cached_span = span;
    ch->state = SPL_LIFECYCLE_ACTIVE;

    return SPL_OK;
}

int spl_channel_remap_partition(spl_session_t *sess, spl_channel_t *ch,
                                uint32_t new_partition_id) {
    if (!sess || !ch) return SPL_ERR_INVALID_STATE;
    if (ch->state != SPL_LIFECYCLE_ACTIVE) return SPL_ERR_INVALID_STATE;

    /* Update partition mapping.
     * does not notify stream engine to resize buffers. */
    ch->partition_id = new_partition_id;

    return SPL_OK;
}

int spl_channel_dispatch_to_span(spl_session_t *sess, spl_channel_t *ch,
                                 const uint8_t *data, size_t len) {
    if (!sess || !ch) return SPL_ERR_INVALID_STATE;
    if (ch->state != SPL_LIFECYCLE_ACTIVE) return SPL_ERR_INVALID_STATE;

    /* Use cached span pointer.
     * cached_span may be dangling if span was freed after rebalance. */
    spl_span_t *span = ch->cached_span;
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Write data to the active segment */
    if (span->segment_count > 0) {
        spl_segment_t *seg = span->segments[span->segment_count - 1];
        if (seg && seg->state == SPL_LIFECYCLE_ACTIVE) {
            return spl_segment_write(seg, data, len);
        }
    }

    return SPL_ERR_NOT_FOUND;
}
