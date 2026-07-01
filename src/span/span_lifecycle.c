#include "../common/types.h"
#include "../common/alloc.h"

int spl_span_lifecycle_incref(spl_span_t *span) {
    if (!span) return SPL_ERR_INVALID_STATE;
    if (span->state == SPL_LIFECYCLE_FREED) return SPL_ERR_INVALID_STATE;
    span->refcount++;
    return SPL_OK;
}

int spl_span_lifecycle_decref(spl_session_t *sess, spl_span_t *span) {
    if (!sess || !span) return SPL_ERR_INVALID_STATE;
    span->refcount--;
    if (span->refcount <= 0) {
        /* Free all segments owned by this span */
        spl_segment_gc_span(sess, span);
        /* Destroy span itself */
        return spl_span_registry_destroy(sess, span);
    }
    return SPL_OK;
}

int spl_span_lifecycle_bump_generation(spl_span_t *span) {
    if (!span) return SPL_ERR_INVALID_STATE;
    span->generation++;
    return SPL_OK;
}

int spl_span_lifecycle_set_state(spl_span_t *span, spl_lifecycle_state_t st) {
    if (!span) return SPL_ERR_INVALID_STATE;
    span->state = st;
    return SPL_OK;
}

int spl_span_lifecycle_deferred_destroy(spl_session_t *sess, spl_span_t *span) {
    if (!sess || !span) return SPL_ERR_INVALID_STATE;
    if (sess->deferred_delete_count >= SPL_MAX_SPANS)
        return SPL_ERR_FULL;

    span->state = SPL_LIFECYCLE_DEFERRED;
    sess->deferred_delete_list[sess->deferred_delete_count++] = span;
    return SPL_OK;
}

int spl_span_handle_create(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return SPL_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;  /* span_id encoded in route_id field */
    uint32_t owner = f->owner_token;

    if (span_id == 0) return SPL_ERR_INVALID_ID;

    spl_span_t *span = spl_span_registry_create(sess, span_id, owner);
    if (!span) return SPL_ERR_NOMEM;

    /* Process subrecords for initial metadata */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == SPL_SUB_META && sub->payload_len >= 8) {
            span->partition_id = spl_read_u32(sub->payload);
            span->route_id = spl_read_u32(sub->payload + 4);
        }
    }

    return SPL_OK;
}

int spl_span_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;
    if (span->owner_token != f->owner_token)
        return SPL_ERR_PERMISSION;

    /* Invalidate all cursors on this span (correct behavior on explicit close) */
    spl_cursor_invalidate_for_span(sess, span_id);

    span->state = SPL_LIFECYCLE_CLOSING;
    return spl_span_lifecycle_decref(sess, span);
}

int spl_span_handle_meta_update(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == SPL_SUB_META && sub->payload_len >= 8) {
            span->retention_watermark = spl_read_u64(sub->payload);
        }
        if (sub->kind == SPL_SUB_GEN_STAMP) {
            spl_span_lifecycle_bump_generation(span);
        }
    }

    return SPL_OK;
}
