#include "../common/types.h"

int spl_retention_init(spl_session_t *sess, uint32_t span_id,
                       uint64_t threshold) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < SPL_MAX_SPANS; i++) {
        if (!sess->retention[i].active) {
            sess->retention[i].span_id = span_id;
            sess->retention[i].gc_threshold_bytes = threshold;
            sess->retention[i].watermark_position = 0;
            sess->retention[i].gc_eligible_count = 0;
            sess->retention[i].active = true;
            return SPL_OK;
        }
    }
    return SPL_ERR_FULL;
}

int spl_retention_check_eligible(spl_session_t *sess, uint32_t span_id,
                                 bool *eligible) {
    if (!sess || !eligible) return SPL_ERR_INVALID_STATE;
    *eligible = false;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    for (uint16_t i = 0; i < SPL_MAX_SPANS; i++) {
        if (sess->retention[i].active && sess->retention[i].span_id == span_id) {
            if (span->total_bytes > sess->retention[i].gc_threshold_bytes)
                *eligible = true;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}
