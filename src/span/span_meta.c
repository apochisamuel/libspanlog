#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Span metadata operations: querying, serializing, and
 * comparing span descriptors for replication and migration.
 */

typedef struct {
    uint32_t span_id;
    uint32_t generation;
    uint32_t epoch_created;
    uint32_t segment_count;
    uint64_t total_bytes;
    uint32_t owner_token;
    uint32_t partition_id;
    uint32_t route_id;
    uint8_t  lifecycle_state;
    int32_t  refcount;
} spl_span_descriptor_t;

static int spl_span_meta_serialize(const spl_span_t *span,
                                   spl_span_descriptor_t *desc) {
    if (!span || !desc) return SPL_ERR_INVALID_STATE;

    desc->span_id = span->span_id;
    desc->generation = span->generation;
    desc->epoch_created = span->epoch_created;
    desc->segment_count = span->segment_count;
    desc->total_bytes = span->total_bytes;
    desc->owner_token = span->owner_token;
    desc->partition_id = span->partition_id;
    desc->route_id = span->route_id;
    desc->lifecycle_state = (uint8_t)span->state;
    desc->refcount = span->refcount;

    return SPL_OK;
}

int spl_span_meta_compare(const spl_span_t *a, const spl_span_t *b) {
    if (!a || !b) return -1;

    spl_span_descriptor_t da, db;
    spl_span_meta_serialize(a, &da);
    spl_span_meta_serialize(b, &db);

    /* Compare by generation first, then by total bytes */
    if (da.generation != db.generation)
        return (int)(da.generation - db.generation);

    if (da.total_bytes != db.total_bytes)
        return da.total_bytes < db.total_bytes ? -1 : 1;

    return 0;
}

uint64_t spl_span_meta_compute_size(const spl_span_t *span) {
    if (!span) return 0;
    uint64_t total = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i])
            total += span->segments[i]->used;
    }
    return total;
}

int spl_span_meta_validate(const spl_span_t *span) {
    if (!span) return SPL_ERR_INVALID_STATE;
    if (span->span_id == 0) return SPL_ERR_INVALID_ID;
    if (span->state == SPL_LIFECYCLE_FREED) return SPL_ERR_INVALID_STATE;
    if (span->refcount < 0) return SPL_ERR_UNDERFLOW;
    if (span->segment_count > SPL_MAX_SEGMENTS) return SPL_ERR_OVERFLOW;
    return SPL_OK;
}

uint32_t spl_span_meta_compute_hash(const spl_span_t *span) {
    if (!span) return 0;
    uint32_t h = spl_hash_mix(span->span_id, span->generation);
    h = spl_hash_mix(h, span->epoch_created);
    h = spl_hash_mix(h, (uint32_t)span->total_bytes);
    h = spl_hash_mix(h, span->segment_count);
    return h;
}
