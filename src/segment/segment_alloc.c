#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_segment_id = 1;

spl_segment_t *spl_segment_alloc(spl_session_t *sess, uint32_t span_id,
                                 size_t capacity) {
    if (!sess) return NULL;
    if (sess->segment_count >= SPL_MAX_SEGMENTS) return NULL;
    if (capacity == 0 || capacity > SPL_MAX_SEGMENT_SIZE) return NULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return NULL;

    spl_segment_t *seg = spl_calloc(1, sizeof(spl_segment_t));
    if (!seg) return NULL;

    seg->data = spl_alloc(capacity);
    if (!seg->data) {
        spl_free(seg);
        return NULL;
    }

    seg->segment_id = next_segment_id++;
    seg->span_id = span_id;
    seg->generation = span->generation;
    seg->capacity = capacity;
    seg->used = 0;
    seg->state = SPL_LIFECYCLE_ACTIVE;
    seg->compacted = false;

    /* Register in span */
    if (span->segment_count < SPL_MAX_SEGMENTS) {
        span->segments[span->segment_count++] = seg;
    }

    /* Register in session */
    sess->segments[sess->segment_count++] = seg;

    return seg;
}

int spl_segment_free(spl_session_t *sess, spl_segment_t *seg) {
    if (!sess || !seg) return SPL_ERR_INVALID_STATE;

    seg->state = SPL_LIFECYCLE_FREED;
    spl_free(seg->data);
    seg->data = NULL;
    seg->capacity = 0;
    seg->used = 0;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] == seg) {
            sess->segments[i] = NULL;
            break;
        }
    }

    /* Don't free the struct yet — callers may still reference it
     * for cleanup. Mark as freed and let GC collect. */
    return SPL_OK;
}

/*
 * Reallocate segment data buffer.
 * This frees the old data and allocates a new buffer.
 * become dangling after this call.
 */
int spl_segment_realloc(spl_session_t *sess, spl_segment_t *seg,
                        size_t new_capacity) {
    if (!sess || !seg) return SPL_ERR_INVALID_STATE;
    if (seg->state != SPL_LIFECYCLE_ACTIVE) return SPL_ERR_INVALID_STATE;
    if (new_capacity == 0 || new_capacity > SPL_MAX_SEGMENT_SIZE)
        return SPL_ERR_INVALID_FRAME;

    /* Allocate new buffer */
    uint8_t *new_data = spl_alloc(new_capacity);
    if (!new_data) return SPL_ERR_NOMEM;

    /* Copy existing data up to min of used and new_capacity */
    size_t copy_len = seg->used;
    if (copy_len > new_capacity) copy_len = new_capacity;
    if (copy_len > 0)
        memcpy(new_data, seg->data, copy_len);

    /* Free old data — any cached pointers to seg->data are now dangling */
    spl_free(seg->data);

    seg->data = new_data;
    seg->capacity = new_capacity;
    if (seg->used > new_capacity) seg->used = new_capacity;
    seg->generation++;

    return SPL_OK;
}

int spl_segment_write(spl_segment_t *seg, const uint8_t *data, size_t len) {
    if (!seg || !data) return SPL_ERR_INVALID_STATE;
    if (seg->state != SPL_LIFECYCLE_ACTIVE) return SPL_ERR_INVALID_STATE;
    if (seg->used + len > seg->capacity) return SPL_ERR_OVERFLOW;

    memcpy(seg->data + seg->used, data, len);
    seg->used += len;
    return SPL_OK;
}

int spl_segment_handle_attach(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Extract capacity from subrecord */
    size_t capacity = 4096;  /* default */
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_SEGMENT_REF && sub->payload_len >= 4) {
            uint32_t req_cap = spl_read_u32(sub->payload);
            if (req_cap > 0 && req_cap <= SPL_MAX_SEGMENT_SIZE)
                capacity = req_cap;
        }
    }

    spl_segment_t *seg = spl_segment_alloc(sess, span_id, capacity);
    if (!seg) return SPL_ERR_NOMEM;

    /* Write initial data from subrecord payloads */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == SPL_SUB_DATA && sub->payload_len > 0) {
            spl_segment_write(seg, sub->payload, sub->payload_len);
        }
    }

    return SPL_OK;
}

int spl_segment_handle_detach(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Find segment by index from subrecord */
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_SEGMENT_REF && sub->payload_len >= 4) {
            uint32_t seg_idx = spl_read_u32(sub->payload);
            if (seg_idx < span->segment_count && span->segments[seg_idx]) {
                spl_segment_free(sess, span->segments[seg_idx]);
                span->segments[seg_idx] = NULL;
            }
        }
    }

    return SPL_OK;
}
