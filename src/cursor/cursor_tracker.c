#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_cursor_id = 1;

spl_cursor_t *spl_cursor_open(spl_session_t *sess, uint32_t span_id,
                              uint32_t lease_id) {
    if (!sess) return NULL;
    if (sess->cursor_count >= SPL_MAX_CURSORS) return NULL;
    if (!(sess->capabilities & SPL_CAP_CURSOR)) return NULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return NULL;

    spl_lease_t *lease = spl_lease_lookup(sess, lease_id);
    if (!lease || !lease->active) return NULL;
    if (lease->span_id != span_id) return NULL;

    spl_cursor_t *cursor = spl_calloc(1, sizeof(spl_cursor_t));
    if (!cursor) return NULL;

    cursor->cursor_id = next_cursor_id++;
    cursor->span_id = span_id;
    cursor->lease_id = lease_id;
    cursor->generation = span->generation;
    cursor->position = 0;
    cursor->segment_index = 0;
    cursor->cached_segment_data = NULL;
    cursor->cached_data_len = 0;
    cursor->active = true;
    cursor->invalidated = false;

    sess->cursors[sess->cursor_count++] = cursor;
    return cursor;
}

int spl_cursor_close(spl_session_t *sess, spl_cursor_t *cursor) {
    if (!sess || !cursor) return SPL_ERR_INVALID_STATE;

    cursor->active = false;
    cursor->cached_segment_data = NULL;
    cursor->cached_data_len = 0;

    return SPL_OK;
}

spl_cursor_t *spl_cursor_lookup(spl_session_t *sess, uint32_t cursor_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        if (sess->cursors[i] && sess->cursors[i]->cursor_id == cursor_id)
            return sess->cursors[i];
    }
    return NULL;
}

int spl_cursor_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;

    /* Find lease from subrecord */
    uint32_t lease_id = 0;
    if (f->parsed_subrecord_count > 0) {
        lease_id = f->subrecords[0].lease_token;
    }
    if (lease_id == 0) {
        /* Try to find any active lease for this span */
        spl_lease_t *l = spl_lease_find_by_span(sess, span_id);
        if (l) lease_id = l->lease_id;
    }
    if (lease_id == 0) return SPL_ERR_INVALID_LEASE;

    spl_cursor_t *c = spl_cursor_open(sess, span_id, lease_id);
    if (!c) return SPL_ERR_NOMEM;

    return SPL_OK;
}

int spl_cursor_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t cursor_id = f->channel_id;  /* cursor_id encoded in channel_id field */
    spl_cursor_t *c = spl_cursor_lookup(sess, cursor_id);
    if (!c) return SPL_ERR_NOT_FOUND;

    return spl_cursor_close(sess, c);
}
