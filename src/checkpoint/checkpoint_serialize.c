#include "../common/types.h"

int spl_checkpoint_serialize_segments(spl_session_t *sess, spl_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return SPL_ERR_INVALID_STATE;
    if (ckpt->phase != SPL_CKPT_IN_PROGRESS && ckpt->phase != SPL_CKPT_FLUSHING)
        return SPL_ERR_INVALID_STATE;

    /* Read segment data through snapshotted pointers. */
    for (uint16_t i = 0; i < ckpt->snap_segment_count; i++) {
        spl_segment_t *seg = ckpt->snap_segments[i];
        if (!seg || !seg->data) continue;

        uint32_t hash = spl_crc32c(seg->data, seg->used);
        (void)hash;
    }

    return SPL_OK;
}

int spl_checkpoint_flush(spl_session_t *sess, spl_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return SPL_ERR_INVALID_STATE;

    ckpt->phase = SPL_CKPT_FLUSHING;
    int rc = spl_checkpoint_serialize_segments(sess, ckpt);
    if (rc != SPL_OK) return rc;

    ckpt->phase = SPL_CKPT_COMPLETE;
    ckpt->active = false;
    return SPL_OK;
}
