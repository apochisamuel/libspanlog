#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Lease priority queue: min-heap ordered by expiry time.
 * Used for efficient lease renewal scheduling and expiry detection.
 */

#define SPL_LEASE_HEAP_MAX 256

typedef struct {
    uint32_t lease_id;
    uint32_t expiry_epoch;
    uint32_t priority;
    uint32_t span_id;
} spl_lease_pq_entry_t;

typedef struct {
    spl_lease_pq_entry_t entries[SPL_LEASE_HEAP_MAX];
    uint16_t count;
} spl_lease_pq_t;

static void pq_swap(spl_lease_pq_t *pq, uint16_t a, uint16_t b) {
    spl_lease_pq_entry_t tmp = pq->entries[a];
    pq->entries[a] = pq->entries[b];
    pq->entries[b] = tmp;
}

static void pq_sift_up(spl_lease_pq_t *pq, uint16_t idx) {
    while (idx > 0) {
        uint16_t parent = (idx - 1) / 2;
        if (pq->entries[idx].expiry_epoch < pq->entries[parent].expiry_epoch) {
            pq_swap(pq, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

static void pq_sift_down(spl_lease_pq_t *pq, uint16_t idx) {
    while (2 * idx + 1 < pq->count) {
        uint16_t left = 2 * idx + 1;
        uint16_t right = 2 * idx + 2;
        uint16_t smallest = idx;

        if (left < pq->count &&
            pq->entries[left].expiry_epoch < pq->entries[smallest].expiry_epoch)
            smallest = left;
        if (right < pq->count &&
            pq->entries[right].expiry_epoch < pq->entries[smallest].expiry_epoch)
            smallest = right;

        if (smallest != idx) {
            pq_swap(pq, idx, smallest);
            idx = smallest;
        } else {
            break;
        }
    }
}

int spl_lease_pq_init(spl_lease_pq_t *pq) {
    if (!pq) return -1;
    pq->count = 0;
    return 0;
}

int spl_lease_pq_push(spl_lease_pq_t *pq, uint32_t lease_id,
                      uint32_t expiry_epoch, uint32_t priority,
                      uint32_t span_id) {
    if (!pq || pq->count >= SPL_LEASE_HEAP_MAX) return -1;

    spl_lease_pq_entry_t *e = &pq->entries[pq->count];
    e->lease_id = lease_id;
    e->expiry_epoch = expiry_epoch;
    e->priority = priority;
    e->span_id = span_id;

    pq_sift_up(pq, pq->count);
    pq->count++;
    return 0;
}

int spl_lease_pq_peek(const spl_lease_pq_t *pq,
                      spl_lease_pq_entry_t *out) {
    if (!pq || pq->count == 0 || !out) return -1;
    *out = pq->entries[0];
    return 0;
}

int spl_lease_pq_pop(spl_lease_pq_t *pq, spl_lease_pq_entry_t *out) {
    if (!pq || pq->count == 0) return -1;
    if (out) *out = pq->entries[0];

    pq->count--;
    if (pq->count > 0) {
        pq->entries[0] = pq->entries[pq->count];
        pq_sift_down(pq, 0);
    }
    return 0;
}

int spl_lease_pq_remove(spl_lease_pq_t *pq, uint32_t lease_id) {
    if (!pq) return -1;

    for (uint16_t i = 0; i < pq->count; i++) {
        if (pq->entries[i].lease_id == lease_id) {
            pq->count--;
            if (i < pq->count) {
                pq->entries[i] = pq->entries[pq->count];
                pq_sift_down(pq, i);
                pq_sift_up(pq, i);
            }
            return 0;
        }
    }
    return -1;
}

/*
 * Lease conflict detection: checks if a new lease conflicts with
 * existing leases on the same span based on owner tokens.
 */
int spl_lease_detect_conflict(spl_session_t *sess, uint32_t span_id,
                              uint32_t owner_token) {
    if (!sess) return 0;

    int conflicts = 0;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active) continue;
        if (l->span_id != span_id) continue;
        if (l->owner_token != owner_token) {
            conflicts++;
        }
    }
    return conflicts;
}

/*
 * Find the lease with the earliest expiry for a given span.
 * Returns lease_id or 0 if none found.
 */
uint32_t spl_lease_earliest_expiry(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return 0;

    uint32_t earliest_id = 0;
    uint32_t earliest_epoch = UINT32_MAX;

    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active || l->span_id != span_id) continue;

        uint32_t expiry = l->epoch_acquired + 3;  /* TTL of 3 epochs */
        if (expiry < earliest_epoch) {
            earliest_epoch = expiry;
            earliest_id = l->lease_id;
        }
    }
    return earliest_id;
}
