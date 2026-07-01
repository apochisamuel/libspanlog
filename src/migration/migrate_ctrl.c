#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_migration_id = 1;

int spl_migrate_initiate(spl_session_t *sess, uint32_t span_id,
                         uint32_t target_node) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    if (!(sess->capabilities & SPL_CAP_MIGRATION)) return SPL_ERR_INVALID_STATE;
    if (sess->migration_count >= SPL_MAX_MIGRATIONS) return SPL_ERR_FULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    spl_migration_t *mig = spl_calloc(1, sizeof(spl_migration_t));
    if (!mig) return SPL_ERR_NOMEM;

    mig->migration_id = next_migration_id++;
    mig->span_id = span_id;
    mig->source_node_id = 0;  /* local */
    mig->target_node_id = target_node;
    mig->phase = SPL_MIGRATE_INITIATED;
    mig->epoch_started = sess->epoch;
    mig->owner_token = span->owner_token;
    mig->active = true;
    mig->total_bytes = span->total_bytes;

    span->migration_id = mig->migration_id;
    span->migration_source = true;
    spl_span_lifecycle_set_state(span, SPL_LIFECYCLE_MIGRATING);

    sess->migrations[sess->migration_count++] = mig;
    return SPL_OK;
}

int spl_migrate_abort(spl_session_t *sess, spl_migration_t *mig) {
    if (!sess || !mig) return SPL_ERR_INVALID_STATE;

    mig->phase = SPL_MIGRATE_ABORTED;
    mig->active = false;

    spl_span_t *span = spl_span_registry_lookup(sess, mig->span_id);
    if (span) {
        span->migration_source = false;
        spl_span_lifecycle_set_state(span, SPL_LIFECYCLE_ACTIVE);
    }

    return SPL_OK;
}

spl_migration_t *spl_migrate_lookup(spl_session_t *sess, uint32_t migration_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] &&
            sess->migrations[i]->migration_id == migration_id)
            return sess->migrations[i];
    }
    return NULL;
}

spl_migration_t *spl_migrate_find_by_span(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] && sess->migrations[i]->span_id == span_id &&
            sess->migrations[i]->active)
            return sess->migrations[i];
    }
    return NULL;
}

int spl_migrate_handle_initiate(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    uint32_t target_node = 0;

    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == SPL_SUB_MIGRATION_STATE && sub->payload_len >= 4) {
            target_node = spl_read_u32(sub->payload);
        }
    }

    return spl_migrate_initiate(sess, span_id, target_node);
}

int spl_migrate_handle_abort(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_migration_t *mig = spl_migrate_find_by_span(sess, span_id);
    if (!mig) return SPL_ERR_NOT_FOUND;

    return spl_migrate_abort(sess, mig);
}
