#include "../common/types.h"

int spl_backpressure_apply_credits(spl_session_t *sess, uint32_t partition_id,
                                   int32_t delta) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        spl_bp_partition_entry_t *e = sess->bp_entries[i];
        if (!e || e->partition_id != partition_id) continue;
        if (!e->active) continue;

        e->credits += delta;
        if (e->credits > e->max_credits) e->credits = e->max_credits;

        /* Fire callback if registered.
         * callback_ctx may be a freed channel pointer. */
        if (e->callback && e->callback_ctx) {
            e->callback(e->callback_ctx, partition_id, e->credits);
        }

        return SPL_OK;
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_backpressure_debit_partition(spl_session_t *sess, uint32_t partition_id,
                                    int32_t amount) {
    return spl_backpressure_apply_credits(sess, partition_id, -amount);
}

int spl_backpressure_handle_credit(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    int32_t delta = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_CREDIT_GRANT && sub->payload_len >= 8) {
            pid = spl_read_u32(sub->payload);
            delta = (int32_t)spl_read_u32(sub->payload + 4);
        }
    }
    if (pid == 0) return SPL_ERR_INVALID_ID;
    return spl_backpressure_apply_credits(sess, pid, delta);
}

int spl_backpressure_handle_window(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    uint32_t new_window = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_CREDIT_GRANT && sub->payload_len >= 8) {
            pid = spl_read_u32(sub->payload);
            new_window = spl_read_u32(sub->payload + 4);
        }
    }
    if (pid == 0) return SPL_ERR_INVALID_ID;

    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] && sess->bp_entries[i]->partition_id == pid) {
            sess->bp_entries[i]->window_size = new_window;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}
