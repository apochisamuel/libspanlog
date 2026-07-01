#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_lease_id = 1;

spl_lease_t *spl_lease_acquire(spl_session_t *sess, uint32_t span_id,
                               uint32_t owner_token) {
    if (!sess) return NULL;
    if (sess->lease_count >= SPL_MAX_LEASES) return NULL;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return NULL;

    spl_lease_t *lease = spl_calloc(1, sizeof(spl_lease_t));
    if (!lease) return NULL;

    lease->lease_id = next_lease_id++;
    lease->span_id = span_id;
    lease->owner_token = owner_token;
    lease->epoch_acquired = sess->epoch;
    lease->generation = span->generation;
    lease->renewal_count = 0;
    lease->active = true;
    lease->force_released = false;

    /* Increment span refcount for lease */
    spl_span_lifecycle_incref(span);

    sess->leases[sess->lease_count++] = lease;
    return lease;
}

int spl_lease_release(spl_session_t *sess, spl_lease_t *lease) {
    if (!sess || !lease) return SPL_ERR_INVALID_LEASE;
    if (!lease->active) return SPL_ERR_INVALID_STATE;

    lease->active = false;

    /* Decrement span refcount */
    spl_span_t *span = spl_span_registry_lookup(sess, lease->span_id);
    if (span) {
        return spl_span_lifecycle_decref(sess, span);
    }
    return SPL_OK;
}

int spl_lease_renew(spl_session_t *sess, spl_lease_t *lease) {
    if (!sess || !lease) return SPL_ERR_INVALID_LEASE;
    if (!lease->active) return SPL_ERR_INVALID_STATE;

    spl_span_t *span = spl_span_registry_lookup(sess, lease->span_id);
    if (!span) return SPL_ERR_NOT_FOUND;

    lease->renewal_count++;
    lease->generation = span->generation;

    /* Every 3 renewals, bump span generation which can trigger compaction. */
    if (lease->renewal_count % 3 == 0) {
        spl_span_lifecycle_bump_generation(span);
        /* Trigger compaction if span has enough segments */
        if (span->segment_count >= 2) {
            spl_segment_compact(sess, span);
        }
    }

    return SPL_OK;
}

int spl_lease_force_release(spl_session_t *sess, spl_lease_t *lease) {
    if (!sess || !lease) return SPL_ERR_INVALID_LEASE;
    if (!lease->active) return SPL_OK;  /* already released */

    lease->force_released = true;
    return spl_lease_release(sess, lease);
}

spl_lease_t *spl_lease_lookup(spl_session_t *sess, uint32_t lease_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i] && sess->leases[i]->lease_id == lease_id)
            return sess->leases[i];
    }
    return NULL;
}

spl_lease_t *spl_lease_find_by_span(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i] && sess->leases[i]->span_id == span_id &&
            sess->leases[i]->active)
            return sess->leases[i];
    }
    return NULL;
}

int spl_lease_handle_acquire(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->negotiated) return SPL_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    uint32_t owner = f->owner_token;

    spl_lease_t *lease = spl_lease_acquire(sess, span_id, owner);
    if (!lease) return SPL_ERR_NOMEM;

    return SPL_OK;
}

int spl_lease_handle_release(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    /* Find lease by owner token and span */
    uint32_t span_id = f->route_id;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (l && l->active && l->span_id == span_id &&
            l->owner_token == f->owner_token) {
            return spl_lease_release(sess, l);
        }
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_lease_handle_renew(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    spl_lease_t *lease = spl_lease_find_by_span(sess, span_id);
    if (!lease) return SPL_ERR_NOT_FOUND;

    if (f->parsed_subrecord_count > 0) {
        const spl_subrecord_t *sub = &f->subrecords[0];
        if (sub->lease_token != lease->lease_id)
            return SPL_ERR_INVALID_TOKEN;
    }

    return spl_lease_renew(sess, lease);
}
