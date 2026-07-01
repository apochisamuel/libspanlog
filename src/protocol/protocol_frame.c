#include "../common/types.h"

int spl_frame_dispatch(spl_session_t *sess, const spl_parsed_frame_t *frame) {
    if (!sess || !frame) return SPL_ERR_INVALID_FRAME;
    if (frame->frame_type == SPL_FRAME_PADDING) return SPL_OK;

    /* Validate session_id if session is initialized */
    if (sess->initialized && frame->session_id != 0 &&
        frame->session_id != sess->session_id)
        return SPL_ERR_INVALID_ID;

    /* Validate epoch: frame epoch must be <= session epoch */
    /* (encoded in parsed header, checked before dispatch) */

    switch (frame->frame_type) {
    /* Session frames */
    case SPL_FRAME_SESSION_INIT:       return spl_session_handle_init(sess, frame);
    case SPL_FRAME_SESSION_CAP_NEG:    return spl_session_handle_cap_neg(sess, frame);
    case SPL_FRAME_SESSION_EPOCH_ADV:  return spl_session_advance_epoch(sess, frame);
    case SPL_FRAME_SESSION_REKEY:      return spl_session_rotate_context(sess, frame);
    case SPL_FRAME_SESSION_CLOSE:      return spl_session_handle_close(sess, frame);

    /* Span frames */
    case SPL_FRAME_SPAN_CREATE:        return spl_span_handle_create(sess, frame);
    case SPL_FRAME_SPAN_CLOSE:         return spl_span_handle_close(sess, frame);
    case SPL_FRAME_SPAN_META_UPDATE:   return spl_span_handle_meta_update(sess, frame);

    /* Segment frames */
    case SPL_FRAME_SEGMENT_ATTACH:     return spl_segment_handle_attach(sess, frame);
    case SPL_FRAME_SEGMENT_DETACH:     return spl_segment_handle_detach(sess, frame);

    /* Channel frames */
    case SPL_FRAME_CHANNEL_OPEN:       return spl_channel_handle_open(sess, frame);
    case SPL_FRAME_CHANNEL_CLOSE:      return spl_channel_handle_close(sess, frame);
    case SPL_FRAME_CHANNEL_BIND:       return spl_channel_handle_bind(sess, frame);
    case SPL_FRAME_CHANNEL_REMAP:      return spl_channel_handle_remap(sess, frame);
    case SPL_FRAME_CHANNEL_DRAIN:      return spl_channel_handle_drain(sess, frame);

    /* Lease frames */
    case SPL_FRAME_LEASE_ACQUIRE:      return spl_lease_handle_acquire(sess, frame);
    case SPL_FRAME_LEASE_RELEASE:      return spl_lease_handle_release(sess, frame);
    case SPL_FRAME_LEASE_RENEW:        return spl_lease_handle_renew(sess, frame);

    /* Stream frames */
    case SPL_FRAME_STREAM_OPEN:        return spl_stream_handle_open(sess, frame);
    case SPL_FRAME_STREAM_CLOSE:       return spl_stream_handle_close(sess, frame);
    case SPL_FRAME_STREAM_APPEND:      return spl_stream_handle_append(sess, frame);
    case SPL_FRAME_STREAM_READ:        return spl_stream_handle_read(sess, frame);
    case SPL_FRAME_STREAM_FLUSH:       return spl_stream_handle_flush(sess, frame);

    /* Cursor frames */
    case SPL_FRAME_CURSOR_OPEN:        return spl_cursor_handle_open(sess, frame);
    case SPL_FRAME_CURSOR_CLOSE:       return spl_cursor_handle_close(sess, frame);
    case SPL_FRAME_CURSOR_ADVANCE:     return spl_cursor_handle_advance(sess, frame);
    case SPL_FRAME_CURSOR_READ:        return spl_cursor_handle_read(sess, frame);

    /* Replication frames */
    case SPL_FRAME_REPL_SETUP:         return spl_repl_handle_setup(sess, frame);
    case SPL_FRAME_REPL_ADD_MEMBER:    return spl_repl_handle_add_member(sess, frame);
    case SPL_FRAME_REPL_REMOVE_MEMBER: return spl_repl_handle_remove_member(sess, frame);
    case SPL_FRAME_REPL_SYNC_BEGIN:    return spl_repl_handle_sync_begin(sess, frame);
    case SPL_FRAME_REPL_SYNC_DATA:     return spl_repl_handle_sync_data(sess, frame);
    case SPL_FRAME_REPL_CHECK:         return spl_repl_handle_check(sess, frame);

    /* Migration frames */
    case SPL_FRAME_MIGRATE_INITIATE:   return spl_migrate_handle_initiate(sess, frame);
    case SPL_FRAME_MIGRATE_PREPARE:    return spl_migrate_handle_prepare(sess, frame);
    case SPL_FRAME_MIGRATE_TRANSFER:   return spl_migrate_handle_transfer(sess, frame);
    case SPL_FRAME_MIGRATE_HANDOFF:    return spl_migrate_handle_handoff(sess, frame);
    case SPL_FRAME_MIGRATE_ABORT:      return spl_migrate_handle_abort(sess, frame);

    /* Checkpoint frames */
    case SPL_FRAME_CHECKPOINT_BEGIN:    return spl_checkpoint_handle_begin(sess, frame);
    case SPL_FRAME_CHECKPOINT_FLUSH:   return spl_checkpoint_handle_flush(sess, frame);
    case SPL_FRAME_CHECKPOINT_RESTORE: return spl_checkpoint_handle_restore(sess, frame);

    /* Retention frames */
    case SPL_FRAME_RETENTION_EVAL:     return spl_retention_handle_eval(sess, frame);
    case SPL_FRAME_RETENTION_COLLECT:  return spl_retention_handle_collect(sess, frame);

    /* Route frames */
    case SPL_FRAME_ROUTE_ADVERTISE:    return spl_route_handle_advertise(sess, frame);
    case SPL_FRAME_ROUTE_WITHDRAW:     return spl_route_handle_withdraw(sess, frame);
    case SPL_FRAME_ROUTE_REBALANCE:    return spl_route_handle_rebalance(sess, frame);
    case SPL_FRAME_ROUTE_NODE_JOIN:    return spl_route_handle_node_join(sess, frame);
    case SPL_FRAME_ROUTE_NODE_DEPART:  return spl_route_handle_node_depart(sess, frame);

    /* Partition frames */
    case SPL_FRAME_PARTITION_ADD:      return spl_partition_handle_add(sess, frame);
    case SPL_FRAME_PARTITION_REMOVE:   return spl_partition_handle_remove(sess, frame);

    /* Backpressure frames */
    case SPL_FRAME_BP_CREDIT_UPDATE:   return spl_backpressure_handle_credit(sess, frame);
    case SPL_FRAME_BP_WINDOW_ADJUST:   return spl_backpressure_handle_window(sess, frame);

    /* Deferred/GC frames */
    case SPL_FRAME_DRAIN_DEFERRED:     return spl_handle_drain_deferred(sess, frame);
    case SPL_FRAME_GC_TRIGGER:         return spl_handle_gc_trigger(sess, frame);

    default:
        return SPL_ERR_INVALID_FRAME;
    }
}

/* Main ingestion entry point */
int spl_ingest_frames(spl_session_t *sess, const uint8_t *data, size_t len) {
    if (!sess || !data || len == 0) return SPL_ERR_INVALID_FRAME;

    /* Parse session header */
    spl_parsed_header_t hdr;
    int rc = spl_protocol_parse_header(data, len, &hdr);
    if (rc != SPL_OK) return rc;

    /* Session initialization: accept first valid header's session_id */
    if (!sess->initialized) {
        /* Will be initialized by SESSION_INIT frame */
    } else {
        /* Validate session ID matches */
        if (hdr.session_id != sess->session_id)
            return SPL_ERR_INVALID_ID;
        /* Validate epoch is current or advance */
        if (hdr.epoch < sess->epoch)
            return SPL_ERR_INVALID_EPOCH;
    }

    size_t offset = SPL_WIRE_HEADER_SIZE;
    int frames_processed = 0;

    for (uint16_t i = 0; i < hdr.frame_count && offset < len; i++) {
        if (offset + SPL_WIRE_FRAME_HEADER_SIZE > len)
            break;

        spl_parsed_frame_t frame;
        rc = spl_protocol_parse_frame(data + offset, len - offset, &frame);
        if (rc != SPL_OK)
            break;

        /* Validate sequence if session is active */
        if (sess->initialized && frame.frame_type != SPL_FRAME_PADDING) {
            if (frame.sequence != sess->next_sequence &&
                frame.frame_type != SPL_FRAME_SESSION_INIT)
                break;  /* sequence gap */
        }

        rc = spl_frame_dispatch(sess, &frame);
        if (rc != SPL_OK)
            break;

        if (frame.frame_type != SPL_FRAME_PADDING)
            sess->next_sequence = frame.sequence + 1;

        offset += frame.total_length;
        frames_processed++;
    }

    return frames_processed > 0 ? frames_processed : SPL_ERR_PROTOCOL;
}
