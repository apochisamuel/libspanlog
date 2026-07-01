#include "../common/types.h"
#include "../common/alloc.h"

int spl_backpressure_add_partition(spl_session_t *sess, uint32_t partition_id,
                                  int32_t initial_credits, int32_t max_credits) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (sess->bp_entry_count >= SPL_MAX_PARTITIONS) return SPL_ERR_FULL;

    spl_bp_partition_entry_t *entry = spl_calloc(1, sizeof(spl_bp_partition_entry_t));
    if (!entry) return SPL_ERR_NOMEM;

    entry->partition_id = partition_id;
    entry->credits = initial_credits;
    entry->max_credits = max_credits > 0 ? max_credits : 100;
    entry->window_size = 64;
    entry->active = true;

    sess->bp_entries[sess->bp_entry_count++] = entry;
    return SPL_OK;
}

int spl_backpressure_remove_partition(spl_session_t *sess, uint32_t partition_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] &&
            sess->bp_entries[i]->partition_id == partition_id) {
            spl_free(sess->bp_entries[i]);
            sess->bp_entries[i] = NULL;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_backpressure_register_callback(spl_session_t *sess, uint32_t partition_id,
                                       spl_bp_callback_fn fn, void *ctx) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] &&
            sess->bp_entries[i]->partition_id == partition_id) {
            sess->bp_entries[i]->callback = fn;
            sess->bp_entries[i]->callback_ctx = ctx;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}
