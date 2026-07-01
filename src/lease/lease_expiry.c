#include "../common/types.h"

/*
 * Lease expiry policy and enforcement.
 * Handles time-based and epoch-based lease expiration.
 */

int spl_lease_check_expiry(spl_session_t *sess, spl_lease_t *lease) {
    if (!sess || !lease) return SPL_ERR_INVALID_LEASE;
    if (!lease->active) return SPL_ERR_EXPIRED;

    /* Epoch-based expiry: lease expires if session epoch has advanced
     * beyond the lease's acquired epoch by more than 2 */
    if (sess->epoch > lease->epoch_acquired + 2) {
        return SPL_ERR_EXPIRED;
    }

    /* Generation-based expiry: lease expires if span generation has
     * advanced beyond lease's recorded generation by more than 5 */
    spl_span_t *span = spl_span_registry_lookup(sess, lease->span_id);
    if (span && span->generation > lease->generation + 5) {
        return SPL_ERR_EXPIRED;
    }

    return SPL_OK;
}

int spl_lease_expire_all_for_epoch(spl_session_t *sess, uint32_t epoch) {
    if (!sess) return SPL_ERR_INVALID_STATE;
    int expired = 0;

    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active) continue;
        if (l->epoch_acquired < epoch) {
            l->active = false;
            l->force_released = true;
            expired++;
        }
    }

    return expired;
}

int spl_lease_compute_ttl(const spl_lease_t *lease, uint32_t current_epoch) {
    if (!lease || !lease->active) return 0;
    if (current_epoch <= lease->epoch_acquired) return 10;  /* max TTL */
    uint32_t age = current_epoch - lease->epoch_acquired;
    if (age >= 3) return 0;
    return (int)(3 - age);
}
