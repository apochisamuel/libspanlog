#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Span merge and split operations for cluster rebalancing.
 *
 * Merge: combines two adjacent spans into one, redistributing
 *        their segments and updating routes/leases.
 *
 * Split: divides a span at a given offset, creating two new spans
 *        with disjoint segment sets.
 */

/*
 * Validate that two spans can be merged.
 * Requirements:
 *   - Same owner token
 *   - Same partition
 *   - No active migrations on either
 *   - Compatible lifecycle states
 */
int spl_span_can_merge(const spl_span_t *a, const spl_span_t *b) {
    if (!a || !b) return -1;
    if (a->owner_token != b->owner_token) return -1;
    if (a->partition_id != b->partition_id) return -1;
    if (a->migration_source || b->migration_source) return -1;
    if (a->state != SPL_LIFECYCLE_ACTIVE ||
        b->state != SPL_LIFECYCLE_ACTIVE) return -1;
    if (a->segment_count + b->segment_count > SPL_MAX_SEGMENTS) return -1;
    return 0;
}

/*
 * Merge span B into span A.
 * All segments from B are moved to A, and B is marked for deferred deletion.
 */
int spl_span_merge(spl_session_t *sess, spl_span_t *a, spl_span_t *b) {
    if (!sess || !a || !b) return -1;
    int rc = spl_span_can_merge(a, b);
    if (rc != 0) return rc;

    /* Move segments from B to A */
    for (uint16_t i = 0; i < b->segment_count; i++) {
        if (b->segments[i] && a->segment_count < SPL_MAX_SEGMENTS) {
            a->segments[a->segment_count++] = b->segments[i];
            b->segments[i]->span_id = a->span_id;
            b->segments[i] = NULL;
        }
    }

    /* Update totals */
    a->total_bytes += b->total_bytes;
    b->total_bytes = 0;
    b->segment_count = 0;

    /* Bump generation on merged span */
    a->generation++;

    /* Invalidate cursors on B */
    spl_cursor_invalidate_for_span(sess, b->span_id);

    /* Mark B for deletion */
    return spl_span_lifecycle_deferred_destroy(sess, b);
}

/*
 * Validate that a span can be split at the given segment index.
 */
int spl_span_can_split(const spl_span_t *span, uint16_t split_at) {
    if (!span) return -1;
    if (split_at == 0 || split_at >= span->segment_count) return -1;
    if (span->state != SPL_LIFECYCLE_ACTIVE) return -1;
    if (span->migration_source) return -1;
    return 0;
}

/*
 * Split span at the given segment index.
 * Segments [0, split_at) stay in the original span.
 * Segments [split_at, count) move to a new span.
 * Returns the new span, or NULL on failure.
 */
spl_span_t *spl_span_split(spl_session_t *sess, spl_span_t *span,
                            uint16_t split_at, uint32_t new_span_id) {
    if (!sess || !span) return NULL;
    if (spl_span_can_split(span, split_at) != 0) return NULL;

    spl_span_t *new_span = spl_span_registry_create(sess, new_span_id,
                                                     span->owner_token);
    if (!new_span) return NULL;

    /* Move upper segments to new span */
    for (uint16_t i = split_at; i < span->segment_count; i++) {
        if (span->segments[i] && new_span->segment_count < SPL_MAX_SEGMENTS) {
            new_span->segments[new_span->segment_count++] = span->segments[i];
            span->segments[i]->span_id = new_span_id;

            /* Track bytes moved */
            new_span->total_bytes += span->segments[i]->used;
            span->total_bytes -= span->segments[i]->used;

            span->segments[i] = NULL;
        }
    }

    span->segment_count = split_at;
    new_span->partition_id = span->partition_id;
    new_span->route_id = 0;  /* needs new route */

    /* Bump generations */
    span->generation++;
    new_span->generation = span->generation;

    /* Invalidate cursors on original span */
    spl_cursor_invalidate_for_span(sess, span->span_id);

    return new_span;
}

/*
 * Compute the optimal split point for load balancing.
 * Returns segment index that most evenly divides the data.
 */
uint16_t spl_span_optimal_split(const spl_span_t *span) {
    if (!span || span->segment_count < 2) return 0;

    uint64_t total = span->total_bytes;
    uint64_t half = total / 2;
    uint64_t running = 0;
    uint64_t best_diff = total;
    uint16_t best_idx = 1;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i])
            running += span->segments[i]->used;

        uint64_t diff = running > half ? running - half : half - running;
        if (diff < best_diff && i > 0) {
            best_diff = diff;
            best_idx = i;
        }
    }

    return best_idx;
}

/*
 * Compute the load imbalance between spans on the same partition.
 * Returns imbalance score 0-1000 (0 = perfect balance).
 */
int spl_span_partition_imbalance(spl_session_t *sess, uint32_t partition_id) {
    if (!sess) return 0;

    uint64_t sizes[SPL_MAX_SPANS];
    uint32_t count = 0;
    uint64_t total = 0;

    for (uint16_t i = 0; i < sess->span_count; i++) {
        spl_span_t *s = sess->spans[i];
        if (!s || s->state != SPL_LIFECYCLE_ACTIVE) continue;
        if (s->partition_id != partition_id) continue;
        sizes[count] = s->total_bytes;
        total += s->total_bytes;
        count++;
    }

    if (count < 2 || total == 0) return 0;

    uint64_t mean = total / count;
    uint64_t sum_sq_diff = 0;
    for (uint32_t i = 0; i < count; i++) {
        int64_t diff = (int64_t)sizes[i] - (int64_t)mean;
        sum_sq_diff += (uint64_t)(diff * diff);
    }

    /* CV * 1000 = (stddev / mean) * 1000 */
    uint64_t variance = sum_sq_diff / count;
    /* Integer sqrt approximation */
    uint64_t x = variance, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + variance / x) / 2; }

    return mean > 0 ? (int)((x * 1000) / mean) : 0;
}
