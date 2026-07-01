#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_stream_id_gen = 1;

int spl_stream_open(spl_session_t *sess, uint32_t channel_id, uint32_t stream_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (sess->stream_count >= SPL_MAX_STREAMS) return SPL_ERR_FULL;
    if (!(sess->capabilities & SPL_CAP_STREAM)) return SPL_ERR_INVALID_STATE;

    spl_channel_t *ch = spl_channel_lookup(sess, channel_id);
    if (!ch) return SPL_ERR_NOT_FOUND;

    spl_stream_t *stream = spl_calloc(1, sizeof(spl_stream_t));
    if (!stream) return SPL_ERR_NOMEM;

    if (stream_id == 0) stream_id = next_stream_id_gen++;
    stream->stream_id = stream_id;
    stream->channel_id = channel_id;
    stream->span_id = ch->span_id;
    stream->active = true;
    stream->state = SPL_LIFECYCLE_ACTIVE;

    /* Allocate reassembly buffer based on partition max_chunk */
    spl_partition_t *part = spl_partition_lookup(sess, ch->partition_id);
    uint32_t max_chunk = 4096;
    if (part && part->max_chunk_size > 0)
        max_chunk = part->max_chunk_size;

    stream->reassembly_capacity = max_chunk;
    stream->reassembly_buf = spl_alloc(max_chunk);
    if (!stream->reassembly_buf) {
        spl_free(stream);
        return SPL_ERR_NOMEM;
    }
    stream->reassembly_used = 0;
    stream->reassembly_expected_seq = 0;
    stream->partition_max_chunk = max_chunk;

    /* Cache crypto context pointer for fast fragment validation. */
    if (sess->capabilities & SPL_CAP_CRYPTO) {
        stream->cached_crypto_ctx = sess->crypto_ctx;
        stream->cached_crypto_ctx_len = sess->crypto_ctx_len;
        stream->crypto_ctx_generation = sess->crypto_generation;
    }

    sess->streams[sess->stream_count++] = stream;
    return SPL_OK;
}

int spl_stream_close(spl_session_t *sess, spl_stream_t *stream) {
    if (!sess || !stream) return SPL_ERR_INVALID_STATE;

    stream->active = false;
    stream->state = SPL_LIFECYCLE_CLOSED;
    spl_free(stream->reassembly_buf);
    stream->reassembly_buf = NULL;
    spl_free(stream->pending_data);
    stream->pending_data = NULL;

    return SPL_OK;
}

int spl_stream_buffer_append(spl_session_t *sess, spl_stream_t *stream,
                             const uint8_t *data, size_t len) {
    if (!sess || !stream || !data) return SPL_ERR_INVALID_STATE;
    if (!stream->active) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, stream->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Find or create target segment */
    spl_segment_t *target = NULL;
    if (span->segment_count > 0) {
        target = span->segments[span->segment_count - 1];
        if (target && target->used + len > target->capacity)
            target = NULL;
    }

    if (!target) {
        size_t cap = len > 4096 ? spl_next_power_of_2((uint32_t)len) : 4096;
        target = spl_segment_alloc(sess, stream->span_id, cap);
        if (!target) return SPL_ERR_NOMEM;
    }

    /* Buffer the pending write — store pointer to target segment. */
    if (stream->pending_data) spl_free(stream->pending_data);
    stream->pending_data = spl_alloc(len);
    if (!stream->pending_data) return SPL_ERR_NOMEM;
    memcpy(stream->pending_data, data, len);
    stream->pending_len = len;
    stream->pending_target_segment = target;

    /* Also write directly for immediate availability */
    int rc = spl_segment_write(target, data, len);
    if (rc != SPL_OK) return rc;

    span->total_bytes += len;
    return SPL_OK;
}

/*
 * Materialize data from a stream by reading through the associated cursor.
 * This delegates to cursor_materialize which reads through cached segment
 * pointers.
 *
 * which reads through a potentially dangling cached pointer.
 */
int spl_stream_materialize_chunk(spl_session_t *sess, spl_stream_t *stream,
                                 uint8_t *out, size_t out_len,
                                 size_t *bytes_read) {
    if (!sess || !stream || !out || !bytes_read)
        return SPL_ERR_INVALID_STATE;
    if (!stream->active) return SPL_ERR_INVALID_STATE;

    /* Find cursor associated with this stream's span */
    spl_cursor_t *cursor = NULL;
    if (stream->cursor_id != 0) {
        cursor = spl_cursor_lookup(sess, stream->cursor_id);
    }
    if (!cursor) {
        /* Find any active cursor on this span */
        for (uint16_t i = 0; i < sess->cursor_count; i++) {
            spl_cursor_t *c = sess->cursors[i];
            if (c && c->active && c->span_id == stream->span_id) {
                cursor = c;
                stream->cursor_id = c->cursor_id;
                break;
            }
        }
    }

    if (!cursor) {
        *bytes_read = 0;
        return SPL_ERR_NOT_FOUND;
    }

    return spl_cursor_materialize(sess, cursor, out, out_len, bytes_read);
}

spl_stream_t *spl_stream_lookup(spl_session_t *sess, uint32_t stream_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->stream_id == stream_id)
            return sess->streams[i];
    }
    return NULL;
}

int spl_stream_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t channel_id = f->channel_id;
    uint32_t stream_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4) {
        stream_id = spl_read_u32(f->subrecords[0].payload);
    }

    return spl_stream_open(sess, channel_id, stream_id);
}

int spl_stream_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t stream_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4) {
        stream_id = spl_read_u32(f->subrecords[0].payload);
    }
    spl_stream_t *s = spl_stream_lookup(sess, stream_id);
    if (!s) return SPL_ERR_NOT_FOUND;

    return spl_stream_close(sess, s);
}

int spl_stream_handle_append(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    /* Find stream by channel */
    uint32_t channel_id = f->channel_id;
    spl_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return SPL_ERR_NOT_FOUND;

    /* Append data from subrecords */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == SPL_SUB_DATA && sub->payload_len > 0) {
            /* Validate fragment integrity against session crypto context */
            spl_stream_validate_fragment(sess, stream,
                                         sub->payload, sub->payload_len);
            int rc = spl_stream_buffer_append(sess, stream,
                                              sub->payload, sub->payload_len);
            if (rc != SPL_OK) return rc;
        }
    }

    return SPL_OK;
}

int spl_stream_handle_read(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    /* Find stream by channel */
    uint32_t channel_id = f->channel_id;
    spl_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return SPL_ERR_NOT_FOUND;

    /* Read via materialize — THIS IS THE ENTRY POINT for the crash */
    uint8_t buf[4096];
    size_t bytes_read = 0;
    return spl_stream_materialize_chunk(sess, stream, buf, sizeof(buf),
                                        &bytes_read);
}

int spl_stream_handle_flush(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    spl_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return SPL_ERR_NOT_FOUND;

    return spl_stream_flush_pending(sess, stream);
}

/*
 * FIX: Invalidate stream's cursor binding when span segments change.
 * Forces stream to re-resolve cursor on next materialize.
 */
int spl_stream_invalidate_for_span(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->stream_count; i++) {
        spl_stream_t *s = sess->streams[i];
        if (!s || !s->active) continue;
        if (s->span_id == span_id) {
            /* Clear cursor binding — force re-resolution on next read */
            s->cursor_id = 0;
        }
    }
    return SPL_OK;
}
