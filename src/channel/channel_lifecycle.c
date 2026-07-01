#include "../common/types.h"
#include "../common/alloc.h"

int spl_channel_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t channel_id = f->channel_id;
    if (channel_id == 0) return SPL_ERR_INVALID_ID;

    spl_channel_t *ch = spl_channel_mux_alloc(sess, channel_id);
    if (!ch) return SPL_ERR_NOMEM;

    return SPL_OK;
}

int spl_channel_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    spl_channel_t *ch = spl_channel_lookup(sess, channel_id);
    if (!ch) return SPL_ERR_NOT_FOUND;

    ch->state = SPL_LIFECYCLE_CLOSING;
    /* Release associated lease if any */
    if (ch->lease_id != 0) {
        spl_lease_t *l = spl_lease_lookup(sess, ch->lease_id);
        if (l && l->active) {
            spl_lease_release(sess, l);
        }
    }

    ch->state = SPL_LIFECYCLE_CLOSED;
    return SPL_OK;
}

int spl_channel_handle_bind(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    spl_channel_t *ch = spl_channel_lookup(sess, channel_id);
    if (!ch) return SPL_ERR_NOT_FOUND;

    uint32_t span_id = f->route_id;
    uint32_t partition_id = 0;

    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_PARTITION_DESC && sub->payload_len >= 4) {
            partition_id = spl_read_u32(sub->payload);
        }
    }

    return spl_channel_bind_span(sess, ch, span_id, partition_id);
}

int spl_channel_handle_remap(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    spl_channel_t *ch = spl_channel_lookup(sess, channel_id);
    if (!ch) return SPL_ERR_NOT_FOUND;

    uint32_t new_partition = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_PARTITION_DESC && sub->payload_len >= 4) {
            new_partition = spl_read_u32(sub->payload);
        }
    }

    return spl_channel_remap_partition(sess, ch, new_partition);
}

int spl_channel_handle_drain(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    spl_channel_t *ch = spl_channel_lookup(sess, channel_id);
    if (!ch) return SPL_ERR_NOT_FOUND;

    ch->state = SPL_LIFECYCLE_DRAINING;
    if (sess->drain_count < SPL_MAX_CHANNELS) {
        sess->drain_list[sess->drain_count++] = ch;
    }

    return SPL_OK;
}
