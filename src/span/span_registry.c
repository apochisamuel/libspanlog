#include "../common/types.h"
#include "../common/alloc.h"

spl_span_t *spl_span_registry_create(spl_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token) {
    if (!sess) return NULL;
    if (sess->span_count >= SPL_MAX_SPANS) return NULL;

    /* Check for duplicate */
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] && sess->spans[i]->span_id == span_id)
            return NULL;
    }

    spl_span_t *span = spl_calloc(1, sizeof(spl_span_t));
    if (!span) return NULL;

    span->span_id = span_id;
    span->owner_token = owner_token;
    span->generation = 1;
    span->epoch_created = sess->epoch;
    span->state = SPL_LIFECYCLE_ACTIVE;
    span->refcount = 1;  /* creation ref */

    sess->spans[sess->span_count++] = span;
    return span;
}

spl_span_t *spl_span_registry_lookup(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] && sess->spans[i]->span_id == span_id &&
            sess->spans[i]->state != SPL_LIFECYCLE_FREED)
            return sess->spans[i];
    }
    return NULL;
}

int spl_span_registry_destroy(spl_session_t *sess, spl_span_t *span) {
    if (!sess || !span) return SPL_ERR_INVALID_STATE;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] == span) {
            /* Don't compact, just null out */
            span->state = SPL_LIFECYCLE_FREED;
            spl_free(span);
            sess->spans[i] = NULL;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}
