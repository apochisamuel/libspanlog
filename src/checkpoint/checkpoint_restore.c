#include "../common/types.h"

int spl_checkpoint_restore(spl_session_t *sess, spl_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return SPL_ERR_INVALID_STATE;

    ckpt->phase = SPL_CKPT_RESTORING;

    spl_span_t *span = spl_span_registry_lookup(sess, ckpt->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Restore segment state from snapshot */
    for (uint16_t i = 0; i < ckpt->snap_segment_count; i++) {
        spl_segment_t *seg = ckpt->snap_segments[i];
        if (seg && seg->state != SPL_LIFECYCLE_FREED) {
            seg->generation = ckpt->generation_at_start;
        }
    }

    span->generation = ckpt->generation_at_start;
    ckpt->phase = SPL_CKPT_COMPLETE;
    return SPL_OK;
}
