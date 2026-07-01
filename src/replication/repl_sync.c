#include "../common/types.h"
#include "../common/alloc.h"

int spl_repl_snapshot_segments(spl_session_t *sess, spl_replica_set_t *rs) {
    if (!sess || !rs) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, rs->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Snapshot raw segment pointers. */
    if (rs->snap_segments) spl_free(rs->snap_segments);
    rs->snap_segments = spl_calloc(span->segment_count, sizeof(spl_segment_t *));
    if (!rs->snap_segments) return SPL_ERR_NOMEM;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        rs->snap_segments[i] = span->segments[i];
    }
    rs->snap_segment_count = span->segment_count;
    rs->snap_generation = span->generation;
    rs->snap_active = true;

    return SPL_OK;
}

int spl_repl_snapshot_replica_set(spl_session_t *sess, spl_replica_set_t *rs) {
    if (!sess || !rs) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, rs->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Snapshot span metadata pointer. */
    rs->snap_span = span;
    rs->snap_generation = span->generation;
    rs->snap_active = true;

    return SPL_OK;
}

int spl_repl_serialize_segment(spl_session_t *sess, spl_replica_set_t *rs,
                               uint32_t seg_idx) {
    if (!sess || !rs) return SPL_ERR_INVALID_STATE;
    if (!rs->snap_active) return SPL_ERR_INVALID_STATE;
    if (seg_idx >= rs->snap_segment_count) return SPL_ERR_INVALID_ID;

    /* Read segment data through snapshot pointer. */
    spl_segment_t *seg = rs->snap_segments[seg_idx];
    if (!seg || !seg->data) return SPL_ERR_NOT_FOUND;

    uint32_t hash = spl_fnv1a(seg->data, seg->used);
    (void)hash;

    return SPL_OK;
}

int spl_repl_handle_sync_begin(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    spl_replica_set_t *rs = spl_repl_find_by_span(sess, span_id);
    if (!rs) return SPL_ERR_NOT_FOUND;

    spl_repl_snapshot_replica_set(sess, rs);
    return spl_repl_snapshot_segments(sess, rs);
}

int spl_repl_handle_sync_data(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    spl_replica_set_t *rs = spl_repl_find_by_span(sess, span_id);
    if (!rs) return SPL_ERR_NOT_FOUND;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        if (f->subrecords[i].kind == SPL_SUB_SEGMENT_REF &&
            f->subrecords[i].payload_len >= 4) {
            uint32_t idx = spl_read_u32(f->subrecords[i].payload);
            spl_repl_serialize_segment(sess, rs, idx);
        }
    }
    return SPL_OK;
}
