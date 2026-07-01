#include "../common/types.h"

int spl_backpressure_check_window(spl_session_t *sess, uint32_t partition_id,
                                  bool *allowed) {
    if (!sess || !allowed) return SPL_ERR_INVALID_STATE;
    *allowed = false;

    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        spl_bp_partition_entry_t *e = sess->bp_entries[i];
        if (!e || e->partition_id != partition_id) continue;
        if (!e->active) continue;
        *allowed = (e->credits > 0);
        return SPL_OK;
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_backpressure_get_credit_balance(spl_session_t *sess, uint32_t partition_id,
                                        int32_t *balance) {
    if (!sess || !balance) return SPL_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        spl_bp_partition_entry_t *e = sess->bp_entries[i];
        if (e && e->partition_id == partition_id && e->active) {
            *balance = e->credits;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}
