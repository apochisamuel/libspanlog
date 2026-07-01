#include "../common/types.h"
#include "../common/alloc.h"

int spl_partition_add(spl_session_t *sess, uint32_t partition_id,
                      uint32_t max_chunk, uint32_t window_size) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (sess->partition_count >= SPL_MAX_PARTITIONS) return SPL_ERR_FULL;

    spl_partition_t *part = spl_calloc(1, sizeof(spl_partition_t));
    if (!part) return SPL_ERR_NOMEM;

    part->partition_id = partition_id;
    part->max_chunk_size = max_chunk > 0 ? max_chunk : 4096;
    part->credit_balance = 100;
    part->window_size = window_size > 0 ? window_size : 64;
    part->active = true;

    sess->partitions[sess->partition_count++] = part;
    return SPL_OK;
}

int spl_partition_remove(spl_session_t *sess, uint32_t partition_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->partition_count; i++) {
        if (sess->partitions[i] &&
            sess->partitions[i]->partition_id == partition_id) {
            spl_backpressure_remove_partition(sess, partition_id);
            spl_free(sess->partitions[i]);
            sess->partitions[i] = NULL;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

spl_partition_t *spl_partition_lookup(spl_session_t *sess, uint32_t partition_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->partition_count; i++) {
        if (sess->partitions[i] &&
            sess->partitions[i]->partition_id == partition_id &&
            sess->partitions[i]->active)
            return sess->partitions[i];
    }
    return NULL;
}

int spl_partition_handle_add(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t pid = 0, max_chunk = 4096, window = 64;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_PARTITION_DESC && sub->payload_len >= 12) {
            pid = spl_read_u32(sub->payload);
            max_chunk = spl_read_u32(sub->payload + 4);
            window = spl_read_u32(sub->payload + 8);
        }
    }
    if (pid == 0) return SPL_ERR_INVALID_ID;
    return spl_partition_add(sess, pid, max_chunk, window);
}

int spl_partition_handle_remove(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        pid = spl_read_u32(f->subrecords[0].payload);
    if (pid == 0) return SPL_ERR_INVALID_ID;
    return spl_partition_remove(sess, pid);
}
