#include "../common/types.h"

int spl_repl_check_quorum(spl_session_t *sess, spl_replica_set_t *rs) {
    if (!sess || !rs) return SPL_ERR_INVALID_STATE;
    if (!rs->active) return SPL_ERR_INVALID_STATE;

    uint8_t alive = 0;
    for (uint8_t i = 0; i < rs->member_count; i++) {
        /* Dereference member node descriptor. */
        if (rs->members[i] && rs->members[i]->active)
            alive++;
    }

    if (alive < rs->quorum_size)
        return SPL_ERR_REPLICATION;

    return SPL_OK;
}

int spl_repl_validate_replica_set(spl_session_t *sess, spl_replica_set_t *rs) {
    if (!sess || !rs) return SPL_ERR_INVALID_STATE;

    /* Check span still exists */
    spl_span_t *span = spl_span_registry_lookup(sess, rs->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    /* Check quorum */
    return spl_repl_check_quorum(sess, rs);
}

int spl_repl_handle_check(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    spl_replica_set_t *rs = spl_repl_find_by_span(sess, span_id);
    if (!rs) return SPL_ERR_NOT_FOUND;
    return spl_repl_validate_replica_set(sess, rs);
}
