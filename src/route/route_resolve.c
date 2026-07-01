#include "../common/types.h"

/* Additional route resolution utilities */

int spl_route_resolve_by_span(spl_session_t *sess, uint32_t span_id,
                              uint32_t *out_route_id) {
    if (!sess || !out_route_id) return SPL_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->route_count; i++) {
        spl_route_entry_t *e = sess->routes[i];
        if (e && e->active && e->span_id == span_id) {
            *out_route_id = e->route_id;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_route_count_for_partition(spl_session_t *sess, uint32_t partition_id) {
    if (!sess) return 0;
    int count = 0;
    for (uint16_t i = 0; i < sess->route_count; i++) {
        if (sess->routes[i] && sess->routes[i]->active &&
            sess->routes[i]->partition_id == partition_id)
            count++;
    }
    return count;
}
