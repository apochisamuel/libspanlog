#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_rs_id = 1;

int spl_repl_setup(spl_session_t *sess, uint32_t span_id, uint8_t quorum) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (!(sess->capabilities & SPL_CAP_REPLICATION)) return SPL_ERR_INVALID_STATE;
    if (sess->replica_set_count >= SPL_MAX_REPLICAS) return SPL_ERR_FULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    spl_replica_set_t *rs = spl_calloc(1, sizeof(spl_replica_set_t));
    if (!rs) return SPL_ERR_NOMEM;

    rs->replica_set_id = next_rs_id++;
    rs->span_id = span_id;
    rs->quorum_size = quorum > 0 ? quorum : 1;
    rs->active = true;
    span->replica_set_id = rs->replica_set_id;

    sess->replica_sets[sess->replica_set_count++] = rs;
    return SPL_OK;
}

int spl_repl_add_member(spl_session_t *sess, uint32_t replica_set_id,
                        uint32_t node_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    spl_replica_set_t *rs = spl_repl_lookup(sess, replica_set_id);
    if (!rs) return SPL_ERR_NOT_FOUND;
    if (rs->member_count >= SPL_MAX_REPLICAS) return SPL_ERR_FULL;

    spl_node_desc_t *node = spl_node_lookup(sess, node_id);
    if (!node) return SPL_ERR_NOT_FOUND;

    /* Store node pointer in replica set. */
    rs->members[rs->member_count++] = node;
    return SPL_OK;
}

int spl_repl_remove_member(spl_session_t *sess, uint32_t replica_set_id,
                           uint32_t node_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    spl_replica_set_t *rs = spl_repl_lookup(sess, replica_set_id);
    if (!rs) return SPL_ERR_NOT_FOUND;

    for (uint8_t i = 0; i < rs->member_count; i++) {
        if (rs->members[i] && rs->members[i]->node_id == node_id) {
            rs->members[i] = rs->members[rs->member_count - 1];
            rs->members[rs->member_count - 1] = NULL;
            rs->member_count--;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

spl_replica_set_t *spl_repl_lookup(spl_session_t *sess, uint32_t replica_set_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->replica_set_count; i++) {
        if (sess->replica_sets[i] &&
            sess->replica_sets[i]->replica_set_id == replica_set_id)
            return sess->replica_sets[i];
    }
    return NULL;
}

spl_replica_set_t *spl_repl_find_by_span(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->replica_set_count; i++) {
        if (sess->replica_sets[i] &&
            sess->replica_sets[i]->span_id == span_id)
            return sess->replica_sets[i];
    }
    return NULL;
}

int spl_repl_handle_setup(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    uint8_t quorum = 1;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 1)
        quorum = f->subrecords[0].payload[0];
    return spl_repl_setup(sess, span_id, quorum);
}

int spl_repl_handle_add_member(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t rs_id = 0;
    uint32_t node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_REPLICA_DESC && sub->payload_len >= 8) {
            rs_id = spl_read_u32(sub->payload);
            node_id = spl_read_u32(sub->payload + 4);
        }
    }
    if (rs_id == 0 || node_id == 0) return SPL_ERR_INVALID_ID;
    return spl_repl_add_member(sess, rs_id, node_id);
}

int spl_repl_handle_remove_member(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t rs_id = 0, node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_REPLICA_DESC && sub->payload_len >= 8) {
            rs_id = spl_read_u32(sub->payload);
            node_id = spl_read_u32(sub->payload + 4);
        }
    }
    return spl_repl_remove_member(sess, rs_id, node_id);
}
