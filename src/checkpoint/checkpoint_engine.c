#include "../common/types.h"
#include "../common/alloc.h"

int spl_checkpoint_begin(spl_session_t *sess, uint32_t span_id,
                         uint32_t ckpt_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (!(sess->capabilities & SPL_CAP_CHECKPOINT)) return SPL_ERR_INVALID_STATE;
    if (sess->checkpoint_count >= SPL_MAX_CHECKPOINTS) return SPL_ERR_FULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    spl_checkpoint_t *ckpt = spl_calloc(1, sizeof(spl_checkpoint_t));
    if (!ckpt) return SPL_ERR_NOMEM;

    ckpt->checkpoint_id = ckpt_id;
    ckpt->span_id = span_id;
    ckpt->phase = SPL_CKPT_IN_PROGRESS;
    ckpt->epoch_started = sess->epoch;
    ckpt->generation_at_start = span->generation;
    ckpt->active = true;

    /* Snapshot segment pointers for this checkpoint. */
    for (uint16_t i = 0; i < span->segment_count && i < SPL_MAX_SEGMENTS; i++) {
        ckpt->snap_segments[i] = span->segments[i];
    }
    ckpt->snap_segment_count = span->segment_count;

    sess->checkpoints[sess->checkpoint_count++] = ckpt;
    return SPL_OK;
}

int spl_checkpoint_register_listener(spl_session_t *sess, spl_checkpoint_t *ckpt,
                                     uint32_t lease_id) {
    if (!sess || !ckpt) return SPL_ERR_INVALID_STATE;

    /* Cache lease_id in listener struct.
     * caches a lease_id it doesn't own. */
    ckpt->listener_lease_id = lease_id;
    ckpt->listener_active = true;

    return SPL_OK;
}

int spl_checkpoint_cleanup_listener(spl_session_t *sess, spl_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return SPL_ERR_INVALID_STATE;
    if (!ckpt->listener_active) return SPL_OK;

    /* Release the cached lease.
     * BUG: this lease may already be released. */
    if (ckpt->listener_lease_id != 0) {
        spl_lease_t *l = spl_lease_lookup(sess, ckpt->listener_lease_id);
        if (l) {
            /* Directly decrement span refcount for the cached lease.
             * Does not check lease ownership or active state.
             * If the lease was already released by its owner, this
             * causes a phantom refcount decrement. */
            spl_span_t *span = spl_span_registry_lookup(sess, l->span_id);
            if (span) {
                spl_span_lifecycle_decref(sess, span);
            }
        }
    }

    ckpt->listener_active = false;
    ckpt->listener_lease_id = 0;

    return SPL_OK;
}

spl_checkpoint_t *spl_checkpoint_lookup(spl_session_t *sess, uint32_t ckpt_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
        if (sess->checkpoints[i] &&
            sess->checkpoints[i]->checkpoint_id == ckpt_id)
            return sess->checkpoints[i];
    }
    return NULL;
}

int spl_checkpoint_handle_begin(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = spl_read_u32(f->subrecords[0].payload);
    if (ckpt_id == 0) ckpt_id = sess->checkpoint_count + 1;

    int rc = spl_checkpoint_begin(sess, span_id, ckpt_id);
    if (rc != SPL_OK) return rc;

    /* Register listener with current lease if any */
    spl_lease_t *l = spl_lease_find_by_span(sess, span_id);
    if (l) {
        spl_checkpoint_t *ckpt = spl_checkpoint_lookup(sess, ckpt_id);
        if (ckpt)
            spl_checkpoint_register_listener(sess, ckpt, l->lease_id);
    }

    return SPL_OK;
}

int spl_checkpoint_handle_flush(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = spl_read_u32(f->subrecords[0].payload);
    spl_checkpoint_t *ckpt = spl_checkpoint_lookup(sess, ckpt_id);
    if (!ckpt) return SPL_ERR_NOT_FOUND;

    int rc = spl_checkpoint_flush(sess, ckpt);
    if (rc != SPL_OK) return rc;

    /* Cleanup listener on completion */
    spl_checkpoint_cleanup_listener(sess, ckpt);
    return SPL_OK;
}

int spl_checkpoint_handle_restore(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = spl_read_u32(f->subrecords[0].payload);
    spl_checkpoint_t *ckpt = spl_checkpoint_lookup(sess, ckpt_id);
    if (!ckpt) return SPL_ERR_NOT_FOUND;
    return spl_checkpoint_restore(sess, ckpt);
}
