#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Epoch management: advances session epoch, triggers migration
 * preparation for spans that need rebalancing, garbage-collects
 * drained channels, and invalidates old-epoch leases.
 */
int spl_session_advance_epoch(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return SPL_ERR_INVALID_STATE;

    /* Validate epoch advancement request from subrecord */
    uint32_t requested_epoch = sess->epoch + 1;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->local_epoch != sess->epoch)
            return SPL_ERR_INVALID_EPOCH;
        if (sub->payload_len >= 4) {
            requested_epoch = spl_read_u32(sub->payload);
            if (requested_epoch <= sess->epoch)
                return SPL_ERR_INVALID_EPOCH;
        }
    }

    uint32_t old_epoch = sess->epoch;
    sess->epoch = requested_epoch;

    /* Trigger migration preparation for spans that were marked for rebalance.
     * Migration prep may reallocate segments (this is where the bug manifests:
     * segment reallocation happens here but cursors are not notified). */
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        spl_migration_t *mig = sess->migrations[i];
        if (!mig || !mig->active) continue;
        if (mig->phase == SPL_MIGRATE_INITIATED) {
            /* Epoch advance triggers migration preparation */
            spl_migrate_prepare_source(sess, mig);
            /* FIX: invalidate cursors that cached pointers to reallocated segments */
            spl_cursor_invalidate_for_epoch(sess, mig->span_id, old_epoch, sess->epoch);
        }
    }

    /* GC drained channels */
    spl_session_gc_drained_channels(sess);

    /* Invalidate old-epoch leases */
    spl_session_invalidate_epoch_leases(sess, old_epoch);

    

    return SPL_OK;
}

int spl_session_gc_drained_channels(spl_session_t *sess) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->drain_count; i++) {
        spl_channel_t *ch = sess->drain_list[i];
        if (!ch) continue;
        if (ch->state != SPL_LIFECYCLE_DRAINING) continue;

        /* Free the channel. */
        spl_channel_mux_free(sess, ch);
        sess->drain_list[i] = NULL;
    }

    /* Compact drain list */
    uint16_t write = 0;
    for (uint16_t read = 0; read < sess->drain_count; read++) {
        if (sess->drain_list[read] != NULL)
            sess->drain_list[write++] = sess->drain_list[read];
    }
    sess->drain_count = write;

    return SPL_OK;
}

int spl_session_invalidate_epoch_leases(spl_session_t *sess, uint32_t old_epoch) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *lease = sess->leases[i];
        if (!lease || !lease->active) continue;
        if (lease->epoch_acquired <= old_epoch) {
            /* Force-release old-epoch lease */
            spl_lease_force_release(sess, lease);
        }
    }

    return SPL_OK;
}
