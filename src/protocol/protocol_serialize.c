#include "../common/types.h"
#include "../common/crc.h"
#include "../common/util.h"
#include "../common/alloc.h"

/*
 * Protocol frame serialization: converts internal state changes
 * into wire-format frames for transmission to remote nodes.
 * Used by replication, migration, and checkpoint subsystems.
 */

typedef struct {
    uint8_t  *buffer;
    size_t    capacity;
    size_t    used;
} spl_serialize_ctx_t;

int spl_serialize_init(spl_serialize_ctx_t *ctx, size_t capacity) {
    if (!ctx) return -1;
    ctx->buffer = spl_alloc(capacity);
    if (!ctx->buffer) return -1;
    ctx->capacity = capacity;
    ctx->used = 0;
    return 0;
}

void spl_serialize_destroy(spl_serialize_ctx_t *ctx) {
    if (!ctx) return;
    spl_free(ctx->buffer);
    ctx->buffer = NULL;
}

int spl_serialize_u8(spl_serialize_ctx_t *ctx, uint8_t val) {
    if (!ctx || ctx->used + 1 > ctx->capacity) return -1;
    ctx->buffer[ctx->used++] = val;
    return 0;
}

int spl_serialize_u16(spl_serialize_ctx_t *ctx, uint16_t val) {
    if (!ctx || ctx->used + 2 > ctx->capacity) return -1;
    spl_write_u16(ctx->buffer + ctx->used, val);
    ctx->used += 2;
    return 0;
}

int spl_serialize_u32(spl_serialize_ctx_t *ctx, uint32_t val) {
    if (!ctx || ctx->used + 4 > ctx->capacity) return -1;
    spl_write_u32(ctx->buffer + ctx->used, val);
    ctx->used += 4;
    return 0;
}

int spl_serialize_u64(spl_serialize_ctx_t *ctx, uint64_t val) {
    if (!ctx || ctx->used + 8 > ctx->capacity) return -1;
    spl_write_u64(ctx->buffer + ctx->used, val);
    ctx->used += 8;
    return 0;
}

int spl_serialize_bytes(spl_serialize_ctx_t *ctx, const uint8_t *data,
                        size_t len) {
    if (!ctx || ctx->used + len > ctx->capacity) return -1;
    memcpy(ctx->buffer + ctx->used, data, len);
    ctx->used += len;
    return 0;
}

int spl_serialize_span_meta(spl_serialize_ctx_t *ctx, const spl_span_t *span) {
    if (!ctx || !span) return -1;

    spl_serialize_u32(ctx, span->span_id);
    spl_serialize_u32(ctx, span->generation);
    spl_serialize_u32(ctx, span->epoch_created);
    spl_serialize_u32(ctx, span->owner_token);
    spl_serialize_u16(ctx, span->segment_count);
    spl_serialize_u64(ctx, span->total_bytes);
    spl_serialize_u32(ctx, span->partition_id);
    spl_serialize_u32(ctx, span->route_id);
    spl_serialize_u8(ctx, (uint8_t)span->state);
    spl_serialize_u32(ctx, (uint32_t)span->refcount);
    spl_serialize_u32(ctx, span->replica_set_id);
    spl_serialize_u32(ctx, span->migration_id);

    return 0;
}

int spl_serialize_segment_header(spl_serialize_ctx_t *ctx,
                                 const spl_segment_t *seg) {
    if (!ctx || !seg) return -1;

    spl_serialize_u32(ctx, seg->segment_id);
    spl_serialize_u32(ctx, seg->span_id);
    spl_serialize_u32(ctx, seg->generation);
    spl_serialize_u64(ctx, (uint64_t)seg->capacity);
    spl_serialize_u64(ctx, (uint64_t)seg->used);
    spl_serialize_u8(ctx, (uint8_t)seg->state);
    spl_serialize_u8(ctx, seg->compacted ? 1 : 0);

    /* Content hash for integrity verification */
    uint32_t hash = 0;
    if (seg->data && seg->used > 0)
        hash = spl_crc32c(seg->data, seg->used);
    spl_serialize_u32(ctx, hash);

    return 0;
}

int spl_serialize_lease(spl_serialize_ctx_t *ctx, const spl_lease_t *lease) {
    if (!ctx || !lease) return -1;

    spl_serialize_u32(ctx, lease->lease_id);
    spl_serialize_u32(ctx, lease->span_id);
    spl_serialize_u32(ctx, lease->owner_token);
    spl_serialize_u32(ctx, lease->epoch_acquired);
    spl_serialize_u32(ctx, lease->generation);
    spl_serialize_u32(ctx, lease->renewal_count);
    spl_serialize_u8(ctx, lease->active ? 1 : 0);
    spl_serialize_u8(ctx, lease->force_released ? 1 : 0);

    return 0;
}

int spl_serialize_cursor(spl_serialize_ctx_t *ctx, const spl_cursor_t *cursor) {
    if (!ctx || !cursor) return -1;

    spl_serialize_u32(ctx, cursor->cursor_id);
    spl_serialize_u32(ctx, cursor->span_id);
    spl_serialize_u32(ctx, cursor->lease_id);
    spl_serialize_u32(ctx, cursor->generation);
    spl_serialize_u32(ctx, cursor->position);
    spl_serialize_u32(ctx, cursor->segment_index);
    spl_serialize_u8(ctx, cursor->active ? 1 : 0);
    spl_serialize_u8(ctx, cursor->invalidated ? 1 : 0);

    return 0;
}

int spl_serialize_channel(spl_serialize_ctx_t *ctx, const spl_channel_t *ch) {
    if (!ctx || !ch) return -1;

    spl_serialize_u32(ctx, ch->channel_id);
    spl_serialize_u32(ctx, ch->session_id);
    spl_serialize_u32(ctx, ch->span_id);
    spl_serialize_u32(ctx, ch->partition_id);
    spl_serialize_u32(ctx, ch->lease_id);
    spl_serialize_u8(ctx, (uint8_t)ch->state);
    spl_serialize_u32(ctx, ch->inflight_count);

    return 0;
}

int spl_serialize_session_state(spl_serialize_ctx_t *ctx,
                                const spl_session_t *sess) {
    if (!ctx || !sess) return -1;

    spl_serialize_u32(ctx, sess->session_id);
    spl_serialize_u32(ctx, sess->epoch);
    spl_serialize_u32(ctx, sess->capabilities);
    spl_serialize_u16(ctx, sess->next_sequence);
    spl_serialize_u8(ctx, (uint8_t)sess->state);
    spl_serialize_u8(ctx, sess->initialized ? 1 : 0);
    spl_serialize_u8(ctx, sess->negotiated ? 1 : 0);
    spl_serialize_u16(ctx, sess->span_count);
    spl_serialize_u16(ctx, sess->segment_count);
    spl_serialize_u16(ctx, sess->channel_count);
    spl_serialize_u16(ctx, sess->stream_count);
    spl_serialize_u16(ctx, sess->cursor_count);
    spl_serialize_u16(ctx, sess->lease_count);

    return 0;
}

/*
 * Deserialize span metadata from wire format.
 */
int spl_deserialize_span_meta(const uint8_t *data, size_t len,
                              spl_span_t *out_span) {
    if (!data || !out_span || len < 46) return -1;

    size_t off = 0;
    out_span->span_id = spl_read_u32(data + off); off += 4;
    out_span->generation = spl_read_u32(data + off); off += 4;
    out_span->epoch_created = spl_read_u32(data + off); off += 4;
    out_span->owner_token = spl_read_u32(data + off); off += 4;
    out_span->segment_count = spl_read_u16(data + off); off += 2;
    out_span->total_bytes = spl_read_u64(data + off); off += 8;
    out_span->partition_id = spl_read_u32(data + off); off += 4;
    out_span->route_id = spl_read_u32(data + off); off += 4;
    out_span->state = (spl_lifecycle_state_t)data[off]; off += 1;
    out_span->refcount = (int32_t)spl_read_u32(data + off); off += 4;
    out_span->replica_set_id = spl_read_u32(data + off); off += 4;
    out_span->migration_id = spl_read_u32(data + off); off += 4;

    (void)off;
    return 0;
}

size_t spl_serialize_size(const spl_serialize_ctx_t *ctx) {
    return ctx ? ctx->used : 0;
}

const uint8_t *spl_serialize_data(const spl_serialize_ctx_t *ctx) {
    return ctx ? ctx->buffer : NULL;
}
