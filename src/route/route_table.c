#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_route_id = 1;

int spl_route_advertise(spl_session_t *sess, uint32_t span_id,
                        uint32_t partition_id, uint32_t node_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (sess->route_count >= SPL_MAX_ROUTES) return SPL_ERR_FULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    spl_route_entry_t *entry = spl_calloc(1, sizeof(spl_route_entry_t));
    if (!entry) return SPL_ERR_NOMEM;

    entry->route_id = next_route_id++;
    entry->span_id = span_id;
    entry->partition_id = partition_id;
    entry->node_id = node_id;
    entry->resolved_span = span;
    entry->generation = span->generation;
    entry->active = true;

    span->route_id = entry->route_id;

    sess->routes[sess->route_count++] = entry;
    return SPL_OK;
}

int spl_route_withdraw(spl_session_t *sess, uint32_t route_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    spl_route_entry_t *e = spl_route_lookup(sess, route_id);
    if (!e) return SPL_ERR_NOT_FOUND;
    e->active = false;
    return SPL_OK;
}

spl_span_t *spl_route_resolve_span(spl_session_t *sess, uint32_t route_id) {
    if (!sess) return NULL;
    spl_route_entry_t *e = spl_route_lookup(sess, route_id);
    if (!e || !e->active) return NULL;
    
    return e->resolved_span;
}

int spl_route_update_mapping(spl_session_t *sess, uint32_t route_id,
                             uint32_t new_span_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    spl_route_entry_t *e = spl_route_lookup(sess, route_id);
    if (!e) return SPL_ERR_NOT_FOUND;

    spl_span_t *new_span = spl_span_registry_lookup(sess, new_span_id);
    if (!new_span) return SPL_ERR_NOT_FOUND;

    e->span_id = new_span_id;
    e->resolved_span = new_span;
    e->generation++;

    return SPL_OK;
}

int spl_route_add_node(spl_session_t *sess, uint32_t node_id,
                       const char *addr, uint16_t port) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (sess->node_count >= SPL_MAX_NODES) return SPL_ERR_FULL;

    spl_node_desc_t *node = spl_calloc(1, sizeof(spl_node_desc_t));
    if (!node) return SPL_ERR_NOMEM;

    node->node_id = node_id;
    if (addr) {
        size_t len = strlen(addr);
        if (len >= sizeof(node->addr)) len = sizeof(node->addr) - 1;
        memcpy(node->addr, addr, len);
    }
    node->port = port;
    node->active = true;

    sess->nodes[sess->node_count++] = node;
    return SPL_OK;
}

int spl_route_remove_node(spl_session_t *sess, uint32_t node_id) {
    if (!sess) return SPL_ERR_INVALID_STATE;

    for (uint8_t i = 0; i < sess->node_count; i++) {
        if (sess->nodes[i] && sess->nodes[i]->node_id == node_id) {
            /* BUG: does not notify replication to purge references */
            spl_free(sess->nodes[i]);
            sess->nodes[i] = NULL;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

spl_route_entry_t *spl_route_lookup(spl_session_t *sess, uint32_t route_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->route_count; i++) {
        if (sess->routes[i] && sess->routes[i]->route_id == route_id)
            return sess->routes[i];
    }
    return NULL;
}

spl_node_desc_t *spl_node_lookup(spl_session_t *sess, uint32_t node_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->node_count; i++) {
        if (sess->nodes[i] && sess->nodes[i]->node_id == node_id)
            return sess->nodes[i];
    }
    return NULL;
}

int spl_route_handle_advertise(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    uint32_t partition_id = 0, node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_ROUTE_ENTRY && sub->payload_len >= 8) {
            partition_id = spl_read_u32(sub->payload);
            node_id = spl_read_u32(sub->payload + 4);
        }
    }
    return spl_route_advertise(sess, span_id, partition_id, node_id);
}

int spl_route_handle_withdraw(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t route_id = f->route_id;
    return spl_route_withdraw(sess, route_id);
}

int spl_route_handle_rebalance(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    /* Rebalance triggers route update + migration */
    uint32_t route_id = f->route_id;
    (void)route_id;
    return SPL_OK;
}

int spl_route_handle_node_join(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t node_id = 0;
    uint16_t port = 0;
    char addr[64] = {0};
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_NODE_DESC && sub->payload_len >= 8) {
            node_id = spl_read_u32(sub->payload);
            port = spl_read_u16(sub->payload + 4);
            size_t alen = sub->payload_len - 6;
            if (alen > 63) alen = 63;
            memcpy(addr, sub->payload + 6, alen);
        }
    }
    if (node_id == 0) return SPL_ERR_INVALID_ID;
    return spl_route_add_node(sess, node_id, addr, port);
}

int spl_route_handle_node_depart(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    uint32_t node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_NODE_DESC && sub->payload_len >= 4) {
            node_id = spl_read_u32(sub->payload);
        }
    }
    if (node_id == 0) return SPL_ERR_INVALID_ID;
    return spl_route_remove_node(sess, node_id);
}
