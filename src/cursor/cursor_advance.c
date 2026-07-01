#include "../common/types.h"

/*
 * Cursor advancement: moves cursor position and caches a direct
 * pointer to the target segment's data buffer for fast reads.
 *
 * caches a raw pointer to the segment's data buffer. If the segment
 * is subsequently reallocated (e.g., by migration preparation triggered
 * by epoch advance), this cached pointer becomes dangling.
 */
int spl_cursor_advance_to_segment(spl_session_t *sess, spl_cursor_t *cursor,
                                  uint32_t segment_index) {
    if (!sess || !cursor) return SPL_ERR_INVALID_STATE;
    if (!cursor->active) return SPL_ERR_INVALID_STATE;
    if (cursor->invalidated) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, cursor->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    if (segment_index >= span->segment_count)
        return SPL_ERR_INVALID_ID;

    spl_segment_t *seg = span->segments[segment_index];
    if (!seg) return SPL_ERR_NOT_FOUND;
    if (seg->state != SPL_LIFECYCLE_ACTIVE) return SPL_ERR_INVALID_STATE;

    /* Cache direct pointer to segment data for fast subsequent reads.
     * data buffer. If the segment's data is freed/reallocated by
     * migration or compaction, this pointer dangles. */
    cursor->cached_segment_data = seg->data;
    cursor->cached_data_len = seg->used;
    cursor->segment_index = segment_index;
    cursor->position = 0;
    cursor->generation = seg->generation;

    return SPL_OK;
}

/*
 * Materialize data from cursor's cached segment pointer.
 *
 * the cached pointer which may be dangling if the segment was
 * reallocated after the cursor advanced.
 */
int spl_cursor_materialize(spl_session_t *sess, spl_cursor_t *cursor,
                           uint8_t *out, size_t out_len, size_t *bytes_read) {
    if (!sess || !cursor || !out || !bytes_read)
        return SPL_ERR_INVALID_STATE;
    if (!cursor->active) return SPL_ERR_INVALID_STATE;

    /* Read through cached segment data pointer.
     * If this pointer is dangling (freed segment), this will crash
     * with heap-use-after-free. */
    if (!cursor->cached_segment_data || cursor->cached_data_len == 0) {
        *bytes_read = 0;
        return SPL_OK;
    }

    size_t available = cursor->cached_data_len - cursor->position;
    size_t to_read = available < out_len ? available : out_len;

    memcpy(out, cursor->cached_segment_data + cursor->position, to_read);

    cursor->position += to_read;
    *bytes_read = to_read;

    return SPL_OK;
}

int spl_cursor_handle_advance(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t cursor_id = f->channel_id;
    spl_cursor_t *c = spl_cursor_lookup(sess, cursor_id);
    if (!c) return SPL_ERR_NOT_FOUND;

    uint32_t seg_idx = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_CURSOR_POS && sub->payload_len >= 4) {
            seg_idx = spl_read_u32(sub->payload);
        }
    }

    return spl_cursor_advance_to_segment(sess, c, seg_idx);
}

int spl_cursor_handle_read(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t cursor_id = f->channel_id;
    spl_cursor_t *c = spl_cursor_lookup(sess, cursor_id);
    if (!c) return SPL_ERR_NOT_FOUND;

    /* Read through cursor — this is the crash path */
    uint8_t buf[4096];
    size_t bytes_read = 0;
    return spl_cursor_materialize(sess, c, buf, sizeof(buf), &bytes_read);
}
