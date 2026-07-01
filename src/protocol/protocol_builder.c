#include "../common/types.h"
#include "../common/crc.h"
#include "../common/util.h"
#include "../common/alloc.h"

/*
 * Frame builder utilities: construct valid wire-format frames
 * for protocol operations. Used by migration, replication, and
 * checkpoint subsystems for generating outbound frames.
 */

typedef struct {
    uint8_t  *buffer;
    size_t    capacity;
    size_t    offset;
    uint16_t  frame_count;
    uint32_t  session_id;
    uint32_t  epoch;
} spl_frame_builder_t;

spl_frame_builder_t *spl_builder_create(uint32_t session_id, uint32_t epoch,
                                        size_t max_size) {
    spl_frame_builder_t *fb = spl_calloc(1, sizeof(spl_frame_builder_t));
    if (!fb) return NULL;

    fb->buffer = spl_alloc(max_size);
    if (!fb->buffer) { spl_free(fb); return NULL; }

    fb->capacity = max_size;
    fb->session_id = session_id;
    fb->epoch = epoch;
    fb->frame_count = 0;

    /* Reserve space for header */
    fb->offset = SPL_WIRE_HEADER_SIZE;
    return fb;
}

void spl_builder_destroy(spl_frame_builder_t *fb) {
    if (!fb) return;
    spl_free(fb->buffer);
    spl_free(fb);
}

int spl_builder_add_frame(spl_frame_builder_t *fb, uint8_t frame_type,
                          uint16_t seq, uint16_t channel_id,
                          uint16_t route_id, uint32_t owner_token,
                          uint16_t flags, uint8_t sub_count,
                          const uint8_t *payload, size_t payload_len) {
    if (!fb) return -1;

    size_t frame_size = SPL_WIRE_FRAME_HEADER_SIZE + payload_len;
    if (payload_len > 0) frame_size += 4;  /* payload CRC */

    if (fb->offset + frame_size > fb->capacity) return -1;

    uint8_t *p = fb->buffer + fb->offset;

    spl_write_u16(p, (uint16_t)frame_size);
    p[2] = frame_type;
    spl_write_u16(p + 3, seq);
    spl_write_u32(p + 5, fb->session_id);
    spl_write_u16(p + 9, channel_id);
    spl_write_u16(p + 11, route_id);
    spl_write_u32(p + 13, owner_token);
    spl_write_u16(p + 17, flags);
    p[19] = sub_count;

    if (payload && payload_len > 0) {
        memcpy(p + SPL_WIRE_FRAME_HEADER_SIZE, payload, payload_len);
        uint32_t pcrc = spl_crc32c(p + SPL_WIRE_FRAME_HEADER_SIZE, payload_len);
        spl_write_u32(p + SPL_WIRE_FRAME_HEADER_SIZE + payload_len, pcrc);
    }

    fb->offset += frame_size;
    fb->frame_count++;
    return 0;
}

size_t spl_builder_finalize(spl_frame_builder_t *fb) {
    if (!fb) return 0;

    /* Write header */
    uint8_t *h = fb->buffer;
    spl_write_u32(h, SPL_MAGIC);
    spl_write_u16(h + 4, 0x0100);
    spl_write_u32(h + 6, fb->session_id);
    spl_write_u32(h + 10, fb->epoch);
    spl_write_u16(h + 14, fb->frame_count);
    uint32_t hcrc = spl_crc32c(h, 14);
    spl_write_u32(h + 16, hcrc);

    return fb->offset;
}

const uint8_t *spl_builder_data(const spl_frame_builder_t *fb) {
    return fb ? fb->buffer : NULL;
}

/*
 * Build a single subrecord into a buffer.
 * Returns bytes written.
 */
size_t spl_build_subrecord(uint8_t *buf, size_t buf_len,
                           uint8_t kind, uint32_t local_epoch,
                           uint32_t generation, uint32_t lease_token,
                           const uint8_t *payload, uint16_t payload_len) {
    size_t needed = SPL_WIRE_SUBRECORD_HEADER_SIZE + payload_len;
    if (needed > buf_len) return 0;

    buf[0] = kind;
    spl_write_u16(buf + 1, payload_len);
    spl_write_u32(buf + 3, local_epoch);
    spl_write_u32(buf + 7, generation);
    spl_write_u32(buf + 11, lease_token);

    if (payload && payload_len > 0)
        memcpy(buf + SPL_WIRE_SUBRECORD_HEADER_SIZE, payload, payload_len);

    return needed;
}

/*
 * Validate a frame's structural integrity beyond CRC.
 * Checks field ranges, subrecord consistency, and type-specific constraints.
 */
int spl_protocol_validate_frame_structure(const spl_parsed_frame_t *f) {
    if (!f) return -1;

    /* Type-specific validation */
    switch (f->frame_type) {
    case SPL_FRAME_SESSION_INIT:
        /* Init must be first frame, no subrecords required */
        break;

    case SPL_FRAME_SESSION_CAP_NEG:
        /* Must have at least one subrecord with capabilities */
        if (f->parsed_subrecord_count < 1) return -1;
        if (f->subrecords[0].payload_len < 4) return -1;
        break;

    case SPL_FRAME_SPAN_CREATE:
        /* Must have route_id (span_id) set */
        if (f->route_id == 0) return -1;
        if (f->owner_token == 0) return -1;
        break;

    case SPL_FRAME_SEGMENT_ATTACH:
        /* Must have route_id (span_id) set */
        if (f->route_id == 0) return -1;
        break;

    case SPL_FRAME_LEASE_ACQUIRE:
        if (f->route_id == 0) return -1;
        break;

    case SPL_FRAME_CHANNEL_OPEN:
        if (f->channel_id == 0) return -1;
        break;

    case SPL_FRAME_CURSOR_OPEN:
        if (f->route_id == 0) return -1;
        break;

    case SPL_FRAME_MIGRATE_INITIATE:
        if (f->route_id == 0) return -1;
        break;

    default:
        break;
    }

    /* Validate subrecord kinds are in valid range */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        if (f->subrecords[i].kind == 0 || f->subrecords[i].kind > 0x10)
            return -1;
    }

    return 0;
}

/*
 * Compute frame payload fingerprint for deduplication.
 * Combines frame type, IDs, and subrecord content hashes.
 */
uint64_t spl_protocol_frame_fingerprint(const spl_parsed_frame_t *f) {
    if (!f) return 0;

    uint32_t h1 = spl_hash_mix((uint32_t)f->frame_type, f->session_id);
    h1 = spl_hash_mix(h1, (uint32_t)f->channel_id | ((uint32_t)f->route_id << 16));
    h1 = spl_hash_mix(h1, f->owner_token);

    uint32_t h2 = 0;
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        uint32_t sh = spl_hash_mix((uint32_t)sub->kind, sub->generation);
        if (sub->payload && sub->payload_len > 0) {
            sh = spl_hash_mix(sh, spl_fnv1a(sub->payload, sub->payload_len));
        }
        h2 = spl_hash_mix(h2, sh);
    }

    return ((uint64_t)h1 << 32) | (uint64_t)h2;
}

/*
 * Sequence number validation with wraparound handling.
 * Returns distance from expected to actual, accounting for 16-bit wrap.
 */
int spl_protocol_sequence_distance(uint16_t expected, uint16_t actual) {
    int32_t diff = (int32_t)actual - (int32_t)expected;
    if (diff > 32768) diff -= 65536;
    if (diff < -32768) diff += 65536;
    return (int)diff;
}

/*
 * Frame rate limiting: compute token bucket state.
 * Returns number of tokens available for the given partition.
 */
int spl_protocol_compute_tokens(uint32_t rate_limit, uint32_t window_ms,
                                uint32_t elapsed_ms, uint32_t current_tokens,
                                uint32_t max_tokens) {
    /* Refill tokens based on elapsed time */
    uint32_t refill = (rate_limit * elapsed_ms) / window_ms;
    uint32_t new_tokens = current_tokens + refill;
    if (new_tokens > max_tokens)
        new_tokens = max_tokens;
    return (int)new_tokens;
}
