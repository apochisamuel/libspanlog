#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Segment compaction: merge multiple small segments into one larger one.
 * Triggered by generation bumps and retention evaluation.
 */
int spl_segment_compact(spl_session_t *sess, spl_span_t *span) {
    if (!sess || !span) return SPL_ERR_INVALID_STATE;
    if (span->segment_count < 2) return SPL_OK;  /* nothing to compact */

    /* Calculate total used bytes across segments */
    size_t total_used = 0;
    uint16_t active_count = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i] && span->segments[i]->state == SPL_LIFECYCLE_ACTIVE) {
            total_used += span->segments[i]->used;
            active_count++;
        }
    }

    if (active_count < 2) return SPL_OK;

    /* Allocate compacted segment */
    size_t new_capacity = spl_next_power_of_2((uint32_t)total_used);
    if (new_capacity < total_used) new_capacity = total_used;
    if (new_capacity > SPL_MAX_SEGMENT_SIZE) return SPL_ERR_OVERFLOW;

    spl_segment_t *compacted = spl_segment_alloc(sess, span->span_id, new_capacity);
    if (!compacted) return SPL_ERR_NOMEM;

    /* Copy data from old segments into compacted segment */
    for (uint16_t i = 0; i < span->segment_count; i++) {
        spl_segment_t *old = span->segments[i];
        if (!old || old->state != SPL_LIFECYCLE_ACTIVE || old == compacted)
            continue;

        if (old->used > 0) {
            int rc = spl_segment_write(compacted, old->data, old->used);
            if (rc != SPL_OK) return rc;
        }

        /* Free old segment data.
         * to old segment data, those pointers are now dangling. */
        old->compacted = true;
        /* Free the data buffer directly.
         * The segment struct stays alive with a dangling data pointer.
         * Checkpoint snapshots still reference this struct. */
        spl_free(old->data);
        /* Do NOT null out old->data or zero old->used.
         * This leaves a dangling pointer that checkpoint_serialize
         * will read through. */
        span->segments[i] = NULL;
    }

    /* Rebuild segment list with just the compacted segment */
    span->segment_count = 0;
    span->segments[span->segment_count++] = compacted;
    span->total_bytes = total_used;

    return SPL_OK;
}
